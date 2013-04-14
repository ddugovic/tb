/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <getopt.h>
#include <inttypes.h>
#include "defs.h"
#include "threads.h"

#define HAS_PAWNS

#include "board.h"
#include "probe.c"
#include "board.c"

#define MAX_PIECES 8

#define MAX_STATS 1536

extern int total_work;
extern struct thread_data thread_data[];
extern int numthreads;
extern struct timeval start_time, cur_time;

static long64 *work_g, *work_piv, *work_p, *work_part;

ubyte *table_w, *table_b;

static long64 size, pawnsize;
static long64 begin;
static int slice_threading_low, slice_threading_high;

int numpcs;
int numpawns;
int has_white_pawns, has_black_pawns;
#ifdef SUICIDE
int stalemate_w, stalemate_b;
int last_w, last_b;
#endif
int symmetric, split;

#ifndef SUICIDE
static int white_king, black_king;
#endif
static int white_pcs[MAX_PIECES], black_pcs[MAX_PIECES];
static int white_all[MAX_PIECES], black_all[MAX_PIECES];
static int pt[MAX_PIECES], pw[MAX_PIECES];
#ifndef SUICIDE
static int pcs2[MAX_PIECES];
#endif

static int ply;
static int finished;
static int ply_accurate_w, ply_accurate_b;

static int file;

static int num_saves;
static int cursed_capt[MAX_PIECES];
static int cursed_pawn_capt_w, cursed_pawn_capt_b;
static int has_cursed_capts, has_cursed_pawn_moves;
static int to_fix_w, to_fix_b;

static long64 total_stats_w[MAX_STATS];
static long64 total_stats_b[MAX_STATS];
static long64 global_stats_w[MAX_STATS];
static long64 global_stats_b[MAX_STATS];

#define COPYSIZE 10*1024*1024
ubyte *copybuf = NULL;

#include "genericp.c"

#if defined(REGULAR)
#include "rtbgenp.c"
#elif defined(SUICIDE)
#include "stbgenp.c"
#elif defined(ATOMIC)
#include "atbgenp.c"
#elif defined(LOSER)
#include "ltbgenp.c"
#endif

#define HUGEPAGESIZE 2*1024*1024

struct tb_handle;

struct dtz_map {
  ubyte map[4][256];
  ubyte inv_map[4][256];
  ubyte num[4];
  ubyte max_num;
  ubyte side;
  ubyte ply_accurate_win;
  ubyte ply_accurate_loss;
  ubyte high_freq_max;
};

void init_permute_pawn(int *pcs, int *pt);
ubyte *init_permute_file(int *pcs, int file);
void *permute_pawn_wdl(ubyte *tb_table, int *pcs, int *pt, ubyte *table, ubyte *best, int file, ubyte *v);
long64 estimate_pawn_dtz(int *pcs, int *pt, ubyte *table, ubyte *best, int *bestp, int file, ubyte *v);
void permute_pawn_dtz(ubyte *tb_table, int *pcs, ubyte *table, int bestp, int file, ubyte *v);
struct tb_handle *create_tb(char *tablename, int wdl, int blocksize);
void compress_tb(struct tb_handle *F, unsigned char *data, ubyte *perm, int minfreq, int maxsymbols);
void merge_tb(struct tb_handle *F);
void compress_init_wdl(int *vals, int flags);
void compress_init_dtz(struct dtz_map *map);

static int minfreq = 8;
static int maxsymbols = 4095;
static int only_generate = 0;
static int generate_dtz = 1;
static int generate_wdl = 1;

static char *tablename;

ubyte *transform_v;
ubyte *transform_tbl;

void transform(struct thread_data *thread)
{
  long64 idx;
  long64 end = begin + thread->end;
  ubyte *v = transform_v;

  for (idx = begin + thread->begin; idx < end; idx++) {
    table_w[idx] = v[table_w[idx]];
    table_b[idx] = v[table_b[idx]];
  }
}

void transform_table(struct thread_data *thread)
{
  long64 idx;
  long64 end = thread->end;
  ubyte *v = transform_v;
  ubyte *table = transform_tbl;

  for (idx = thread->begin; idx < end; idx++)
    table[idx] = v[table[idx]];
}

#include "statsp.c"

#include "reducep.c"

void calc_pawn_table_unthreaded(void)
{
  long64 idx, idx2;
  long64 size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  bitboard occ;
  int p[MAX_PIECES];

  set_tbl_to_wdl(0);

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;

  int *old_p = thread_data[0].p;
  thread_data[0].p = p;

  // first perform 50 iterations for each slice
  for (idx = 0; idx < pawnsize*6/6; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5-cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      thread_data[0].occ = occ;
      has_cursed_pawn_moves = 0;
      if (has_white_pawns)
	run_single(calc_pawn_moves_w, work_p, 0);
      if (has_black_pawns)
	run_single(calc_pawn_moves_b, work_p, 0);
#ifndef SUICIDE
      run_single(calc_mates, work_p, 0);
#endif
      iterate();
    } else {
      int local;
      for (local = 0; local < num_saves; local++)
	reduce_tables(local);
    }
  }
  printf("\n");
  thread_data[0].p = old_p;

  if (generate_dtz)
    for (i = 0; i < num_saves; i++) {
      fclose(tmp_table[i][0]);
      fclose(tmp_table[i][1]);
    }

#ifdef SUICIDE
  set_draw_threats();
#endif
}

void calc_pawn_table_threaded(void)
{
  long64 idx, idx2;
  long64 size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  bitboard occ;
  int p[MAX_PIECES];

  set_tbl_to_wdl(0);

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;

  // first perform 50 iterations for each slice
  for (idx = 0; idx < pawnsize * 6/6; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5-cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      for (i = 0; i < numthreads; i++)
	thread_data[i].occ = occ;
      for (i = 0; i < numthreads; i++)
	memcpy(thread_data[i].p, p, MAX_PIECES * sizeof(int));
      has_cursed_pawn_moves = 0;
      if (has_white_pawns)
	run_threaded(calc_pawn_moves_w, work_p, 0);
      if (has_black_pawns)
	run_threaded(calc_pawn_moves_b, work_p, 0);
#ifndef SUICIDE
      run_threaded(calc_mates, work_p, 0);
#endif
      iterate();
    } else {
      int local;
      for (local = 0; local < num_saves; local++)
	reduce_tables(local);
    }
  }
  printf("\n");

  if (generate_dtz)
    for (i = 0; i < num_saves; i++) {
      fclose(tmp_table[i][0]);
      if (!symmetric)
	fclose(tmp_table[i][1]);
    }

#ifdef SUICIDE
  set_draw_threats();
#endif
}

static LOCK_T tc_mutex;
static ubyte *tc_table;
static ubyte *tc_v;
static int tc_capt_closs, tc_closs;

static void tc_loop(struct thread_data *thread)
{
  int i;
  long64 idx = thread->begin;
  long64 end = thread->end;
  ubyte *table = tc_table;
  ubyte *v = tc_v;

  for (; idx < end; idx++)
    if (v[table[idx]]) {
      LOCK(tc_mutex);
      switch (v[table[idx]]) {
      case 1:
	tc_capt_closs = 1;
	v[CAPT_CLOSS] = 0;
	break;
      case 2:
	tc_closs = 1;
	if (num_saves == 0)
	  for (i = DRAW_RULE; i < REDUCE_PLY; i++)
	    v[LOSS_IN_ONE - i] = 0;
	else
	  for (i = LOSS_IN_ONE; i >= LOSS_IN_ONE - REDUCE_PLY_RED - 1; i--)
	    v[i] = 0;
	break;
      }
      if (tc_capt_closs && tc_closs)
	idx = end;
      UNLOCK(tc_mutex);
    }
}

void test_closs(long64 *stats, ubyte *table, int to_fix)
{
  int i;
  ubyte v[256];

  tc_capt_closs = tc_closs = 0;
  if (to_fix) {
    tc_table = table;
    tc_v = v;
    for (i = 0; i < 256; i++)
      v[i] = 0;
    v[CAPT_CLOSS] = 1;
    if (num_saves == 0)
      for (i = DRAW_RULE; i < REDUCE_PLY; i++)
	v[LOSS_IN_ONE - i] = 2;
    else
      for (i = LOSS_IN_ONE; i >= LOSS_IN_ONE - REDUCE_PLY_RED - 1; i--)
	v[i] = 2;
    run_threaded(tc_loop, work_g, 0);
  } else {
    for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
      if (stats[STAT_MATE - i]) break;
    if (i <= MAX_PLY)
      tc_closs = 1;
  }
}

void prepare_wdl_map(long64 *stats, ubyte *v, int pa_w, int pa_l)
{
  int i, j;
  int vals[5];
  int dc[4];

  for (i = 0; i < 5; i++)
    vals[i] = 0;

#ifndef SUICIDE
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[i]) break;
  if (i <= DRAW_RULE || stats[STAT_PAWN_WIN])
    vals[4] = 1;
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[i]) break;
  if (i <= MAX_PLY || stats[STAT_PAWN_CWIN])
    vals[3] = 1;
  if (stats[STAT_DRAW])
    vals[2] = 1;
  if (tc_closs)
    vals[1] = 1;
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i]) break;
  if (i <= DRAW_RULE)
    vals[0] = 1;
#else
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[i]) break;
  if (i <= DRAW_RULE)
    vals[4] = 1;
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[i]) break;
  if (i <= MAX_PLY)
    vals[3] = 1;
  if (stats[STAT_DRAW])
    vals[2] = 1;
  // FIXME: probably should scan the table for non-CAPT_CLOSS cursed losses
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[STAT_MATE - i]) break;
  if (i <= MAX_PLY)
    vals[1] = 1;
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i]) break;
  if (i <= DRAW_RULE)
    vals[0] = 1;
#endif

  for (i = 0; i < 4; i++)
    dc[i] = 0;
#ifndef SUICIDE
  dc[3] = 1;
  dc[2] = (stats[STAT_CAPT_CWIN] != 0);
  dc[1] = (stats[STAT_CAPT_DRAW] != 0);
  dc[0] = tc_capt_closs;
#else
  dc[3] = 1;
  dc[2] = (stats[STAT_THREAT_CWIN2] + stats[STAT_THREAT_CWIN1] != 0);
  dc[1] = (stats[STAT_THREAT_DRAW] != 0);
//  dc[0] = (stats[515] != 0); // THREAT_CLOSS
  dc[0] = 0; // FIXME: CAPT_CLOSS
#endif

  for (i = 0; i < 4; i++)
    if (dc[i]) break;
  for (j = 0; j < 5; j++)
    if (vals[j]) break;
  if (j > i + 1)
    vals[0] = 1;

#ifndef SUICIDE
  v[ILLEGAL] = 8;
  v[BROKEN] = 8;
  v[CAPT_WIN] = 8;
  v[CAPT_DRAW] = 6;
  v[CAPT_CLOSS] = 5;
  v[UNKNOWN] = v[PAWN_DRAW] = 2;
  if (num_saves == 0) {
    v[PAWN_WIN] = 4;
    for (i = 0; i < DRAW_RULE; i++)
      v[WIN_IN_ONE + i] = 4;
    v[CAPT_CWIN] = 7;
    v[PAWN_CWIN] = 3;
    for (i = DRAW_RULE; i <= REDUCE_PLY; i++)
      v[WIN_IN_ONE + i + 1] = 3;
    for (i = DRAW_RULE; i < REDUCE_PLY; i++)
      v[LOSS_IN_ONE - i] = 1;
    for (i = 0; i <= DRAW_RULE; i++)
      v[MATE - i] = 0;
  } else {
    v[WIN_RED] = 4;
    v[CAPT_CWIN_RED] = 7;
    for (i = 0; i <= REDUCE_PLY_RED + 2; i++)
      v[CAPT_CWIN_RED + i + 1] = 3;
    for (i = 0; i <= REDUCE_PLY_RED + 1; i++)
      v[LOSS_IN_ONE - i] = 1;
    v[MATE] = 0;
  }
#else
// FIXME: THREAT_CLOSS
  v[BROKEN] = 8;
  v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = 8;
  v[CAPT_CLOSS] = v[CAPT_LOSS] = 8;
  v[THREAT_DRAW] = 6;
  v[UNKNOWN] = v[PAWN_DRAW] = 2;
  if (num_saves == 0) {
    v[STALE_WIN] = v[STALE_WIN + 1] = 4;
    v[THREAT_WIN1] = v[THREAT_WIN2] = 8;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = 4;
    v[BASE_WIN + DRAW_RULE + 1] = 3;
    v[THREAT_CWIN1] = v[THREAT_CWIN2] = 7;
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY; i++)
      v[BASE_WIN + i + 2] = 3;
    for (i = DRAW_RULE + 1; i <= REDUCE_PLY; i++)
      v[BASE_LOSS - i] = 1;
    for (i = 0; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = 0;
  } else {
    v[THREAT_WIN_RED] = 8;
    v[BASE_WIN_RED] = 4;
    v[THREAT_CWIN_RED] = 7;
    v[BASE_CWIN_RED] = 3;
    v[BASE_LOSS_RED] = 0;
    v[BASE_CLOSS_RED] = 1;
    for (i = 0; i <= REDUCE_PLY_RED; i++) {
      v[BASE_CWIN_RED + 1 + i] = 3;
      v[BASE_CLOSS_RED - 1 - i] = 1;
    }
  }
#endif

  compress_init_wdl(vals, (pa_w << 2) | (pa_l << 3));
}

struct dtz_map map_w[4];
struct dtz_map map_b[4];

int sort_list(long64 *freq, ubyte *map, ubyte *inv_map)
{
  int i, j;
  int num;

  num = 0;
  for (i = 0; i < 256; i++)
    if (freq[i])
      map[num++] = i;

  for (i = 0; i < num; i++)
    for (j = i + 1; j < num; j++)
      if (freq[map[i]] < freq[map[j]]) {
	ubyte tmp = map[i];
	map[i] = map[j];
	map[j] = tmp;
      }
  for (i = 0; i < num; i++)
    inv_map[map[i]] = i;

  return num;
}

void sort_values(long64 *stats, struct dtz_map *dtzmap, int side, int pa_w, int pa_l)
{
  int i, j;
  long64 freq[4][256];
  ubyte (*map)[256] = dtzmap->map;
  ubyte (*inv_map)[256] = dtzmap->inv_map;

  dtzmap->side = side;
  dtzmap->ply_accurate_win = pa_w;
  dtzmap->ply_accurate_loss = pa_l;

  for (j = 0; j < 4; j++)
    for (i = 0; i < 256; i++)
      freq[j][i] = 0;

  freq[0][0] = stats[0];
  if (dtzmap->ply_accurate_win)
    for (i = 0; i < DRAW_RULE; i++)
      freq[0][i] += stats[i + 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      freq[0][i / 2] += stats[i + 1];
  dtzmap->num[0] = sort_list(freq[0], map[0], inv_map[0]);

  freq[1][0] = stats[STAT_MATE];
  if (dtzmap->ply_accurate_loss)
    for (i = 0; i < DRAW_RULE; i++)
      freq[1][i] += stats[STAT_MATE - i - 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      freq[1][i / 2] += stats[STAT_MATE - i - 1];
  dtzmap->num[1] = sort_list(freq[1], map[1], inv_map[1]);

  for (i = DRAW_RULE; i < MAX_PLY; i++)
    freq[2][(i - DRAW_RULE) / 2] += stats[i + 1];
  dtzmap->num[2] = sort_list(freq[2], map[2], inv_map[2]);

  for (i = DRAW_RULE; i < MAX_PLY; i++)
    freq[3][(i - DRAW_RULE) / 2] += stats[STAT_MATE - i - 1];
  dtzmap->num[3] = sort_list(freq[3], map[3], inv_map[3]);

  int num = 1;
  for (i = 0; i < 4; i++)
    if (dtzmap->num[i] > num)
      num = dtzmap->num[i];
  dtzmap->max_num = num;

  long64 tot = 6ULL;
  for (i = 1; i < numpawns; i++)
    tot *= (48ULL - i);
  for (; i < numpcs; i++)
    tot *= (64ULL - i);
  tot /= 7000ULL;

  for (i = 0; i < num; i++) {
    long64 f = 0;
    for (j = 0; j < 4; j++)
      if (i < dtzmap->num[j])
	f += freq[j][map[j][i]];
    if (f < tot) break;
  }
  dtzmap->high_freq_max = i;
}

void prepare_dtz_map(ubyte *v, struct dtz_map *map)
{
  int i;
  ubyte (*inv_map)[256] = map->inv_map;
  int num = map->max_num;

  if (num_saves == 0) {
    for (i = 0; i < 256; i++)
      v[i] = 0;

#ifndef SUICIDE
    v[ILLEGAL] = num;
    v[UNKNOWN] = v[BROKEN] = v[PAWN_DRAW] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
    v[PAWN_WIN] = v[PAWN_CWIN] = num;

    v[MATE] = inv_map[1][0];
    if (map->ply_accurate_win)
      for (i = 0; i < DRAW_RULE; i++)
	v[WIN_IN_ONE + i] = inv_map[0][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
	v[WIN_IN_ONE + i] = inv_map[0][i / 2];
    if (map->ply_accurate_loss)
      for (i = 0; i < DRAW_RULE; i++)
	v[LOSS_IN_ONE - i] = inv_map[1][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
	v[LOSS_IN_ONE - i] = inv_map[1][i / 2];
    for (; i < REDUCE_PLY; i++) {
      v[WIN_IN_ONE + i + 2] = inv_map[2][(i - DRAW_RULE) / 2];
      v[LOSS_IN_ONE - i] = inv_map[3][(i - DRAW_RULE) / 2];
    }
#else
    v[UNKNOWN] = v[BROKEN] = v[PAWN_DRAW] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
    v[CAPT_CLOSS] = v[CAPT_LOSS] = num;
    v[THREAT_WIN1] = v[THREAT_WIN2] = num;
    v[THREAT_CWIN1] = v[THREAT_CWIN2] = num;
    v[THREAT_DRAW] = num;
    v[STALE_WIN] = v[STALE_WIN + 1] = inv_map[0][0];
    if (map->ply_accurate_win)
      for (i = 2; i <= DRAW_RULE; i++)
	v[BASE_WIN + i] = inv_map[0][i - 1];
    else
      for (i = 2; i <= DRAW_RULE; i++)
	v[BASE_WIN + i] = inv_map[0][(i - 1) / 2];
    if (map->ply_accurate_loss)
      for (i = 0; i <= DRAW_RULE; i++)
	v[BASE_LOSS - i] = inv_map[1][i - 1];
    else
      for (i = 2; i <= DRAW_RULE; i++)
	v[BASE_LOSS - i] = inv_map[1][(i - 1) / 2];
    v[BASE_WIN + DRAW_RULE + 1] = inv_map[2][0];
    v[BASE_LOSS - DRAW_RULE - 1] = inv_map[3][0];
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY; i++) {
      v[BASE_WIN + i + 2] = inv_map[2][(i - 1) / 2];
      v[BASE_LOSS - i] = inv_map[3][(i - 1) / 2];
    }
#endif
  } else {
    for (i = 0; i < 256; i++)
      v[i] = i;
  }

  compress_init_dtz(map);
}

extern char *optarg;

static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "wdl", 0, NULL, 'w' },
  { "dtz", 0, NULL, 'z' },
  { "stats", 0, NULL, 's' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int i, j;
  int color;
  int val, longindex;
  int pcs[16];
  ubyte v[256];
  int save_stats = 0;

  numthreads = 1;
  do {
    val = getopt_long(argc, argv, "t:gwzs", options, &longindex);
    switch (val) {
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'g':
      only_generate = 1;
      generate_dtz = generate_wdl = 0;
      break;
    case 'w':
      generate_dtz = 0;
      break;
    case 'z':
      generate_wdl = 0;
      break;
    case 's':
      save_stats = 1;
    }
  } while (val != EOF);

  if (optind >= argc) {
    printf("No tablebase specified.\n");
    exit(0);
  }
  tablename = argv[optind];

  init_tablebases();

  for (i = 0; i < 16; i++)
    pcs[i] = 0;

  numpcs = strlen(tablename) - 1;
  color = 0;
  j = 0;
  for (i = 0; i < strlen(tablename); i++)
    switch (tablename[i]) {
    case 'P':
      pcs[PAWN | color]++;
      pt[j++] = PAWN | color;
      break;
    case 'N':
      pcs[KNIGHT | color]++;
      pt[j++] = KNIGHT | color;
      break;
    case 'B':
      pcs[BISHOP | color]++;
      pt[j++] = BISHOP | color;
      break;
    case 'R':
      pcs[ROOK | color]++;
      pt[j++] = ROOK | color;
      break;
    case 'Q':
      pcs[QUEEN | color]++;
      pt[j++] = QUEEN | color;
      break;
    case 'K':
      pcs[KING | color]++;
      pt[j++] = KING | color;
      break;
    case 'v':
      if (color) exit(1);
      color = 0x08;
      break;
    default:
      exit(1);
    }
  if (!color) exit(1);

  numpawns = pcs[WPAWN] + pcs[BPAWN];
  has_white_pawns = (pcs[WPAWN] != 0);
  has_black_pawns = (pcs[BPAWN] != 0);

  // move pieces to back
  for (i = j = numpcs - 1; i >= numpawns; i--, j--) {
    while ((pt[j] & 0x07) == 1) j--;
    pt[i] = pt[j];
  }
  if (pcs[WPAWN] > 0 && (pcs[BPAWN] == 0 || pcs[WPAWN] <= pcs[BPAWN])) {
    for (i = 0; i < pcs[WPAWN]; i++)
      pt[i] = WPAWN;
    for (; i < numpawns; i++)
      pt[i] = BPAWN;
  } else {
    for (i = 0; i < pcs[BPAWN]; i++)
      pt[i] = BPAWN;
    for (; i < numpawns; i++)
      pt[i] = WPAWN;
  }

  if (numpawns == 0) {
    printf("Expecting pawns.\n");
    exit(1);
  }

  if (numthreads < 1) numthreads = 1;
  else if (numthreads > MAX_THREADS) numthreads = MAX_THREADS;

  printf("number of threads = %d\n", numthreads);

  if (numthreads == 1)
    total_work = 1;
  else
    total_work = 100 + 10 * numthreads;

  slice_threading_low = (numthreads > 1) && (numpcs - numpawns) >= 2;
  slice_threading_high = (numthreads > 1) && (numpcs - numpawns) >= 3;

  size = 6ULL << (6 * (numpcs-1));
  pawnsize = 6ULL << (6 * (numpawns - 1));

  for (i = 0; i < numpcs; i++) {
    shift[i] = (numpcs - i - 1) * 6;
    mask[i] = 0x3fULL << shift[i];
  }

  work_g = create_work(total_work, size, 0x3f);
  work_piv = create_work(total_work, 1ULL << shift[0], 0);
  work_p = create_work(total_work, 1ULL << shift[numpawns - 1], 0x3f);
  work_part = alloc_work(total_work);

#if 1
  static int piece_order[16] = {
    0, 0, 3, 5, 7, 9, 1, 0,
    0, 0, 4, 6, 8, 10, 2, 0
  };

  for (i = numpawns; i < numpcs; i++)
    for (j = i + 1; j < numpcs; j++)
      if (piece_order[pt[i]] > piece_order[pt[j]]) {
	int tmp = pt[i];
	pt[i] = pt[j];
	pt[j] = tmp;
      }
#endif

#ifdef ATOMIC
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] == WKING) break;
  white_all[j++] = i;
  for (i = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08) && pt[i] != WKING)
      white_all[j++] = i;
  white_all[j] = -1;
#else
  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08))
      white_all[j++] = i;
  white_all[j] = -1;
#endif

#ifdef ATOMIC
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] == BKING) break;
  black_all[j++] = i;
  for (i = 0; i < numpcs; i++)
    if ((pt[i] & 0x08) && pt[i] != BKING)
      black_all[j++] = i;
  black_all[j] = -1;
#else
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] & 0x08)
      black_all[j++] = i;
  black_all[j] = -1;
#endif

  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08) && pt[i] != 0x01)
      white_pcs[j++] = i;
  white_pcs[j] = -1;

  for (i = 0, j = 0; i < numpcs; i++)
    if ((pt[i] & 0x08) && pt[i] != 0x09)
      black_pcs[j++] = i;
  black_pcs[j] = -1;

  for (i = 0; i < numpcs; i++)
    pw[i] = (pt[i] == WPAWN) ? 0x38 : 0x00;
  pw_mask = 0;
  for (i = 1; i < numpcs; i++)
    pw_mask |= pw[i] << (6 * (numpcs - i - 1));
  pw_pawnmask = pw_mask >> (6 * (numpcs - numpawns));

  idx_mask1[numpcs - 1] = 0xffffffffffffffc0ULL;
  idx_mask2[numpcs - 1] = 0;
  for (i = numpcs - 2; i >= 0; i--) {
    idx_mask1[i] = idx_mask1[i + 1] << 6;
    idx_mask2[i] = (idx_mask2[i + 1] << 6) | 0x3f;
  }

  for (i = 0; i < numpcs; i++)
    pw_capt_mask[i] = ((pw_mask & idx_mask1[i]) >> 6)
				    | (pw_mask & idx_mask2[i]);

#ifndef SUICIDE
  for (i = 0; i < numpcs; i++)
    if (pt[i] == WKING)
      white_king = i;

  for (i = 0; i < numpcs; i++)
    if (pt[i] == BKING)
      black_king = i;
#else
  int stalemate = 0;
  for (i = 0; i < numpcs; i++)
    stalemate += (pt[i] & 0x08) ? 1 : -1;
  if (stalemate > 0) {
    stalemate_w = STALE_WIN;
    stalemate_b = BASE_LOSS;
  } else if (stalemate < 0) {
    stalemate_w = BASE_LOSS;
    stalemate_b = STALE_WIN;
  } else
    stalemate_w = stalemate_b = UNKNOWN; // should be OK

  last_w = -1;
  j = 0;
  for (i = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08)) j++;
  if (j == 1) {
    for (i = 0; i < numpcs; i++)
      if (!(pt[i] & 0x08)) break;
    last_w = i;
  }
  last_b = -1;
  j = 0;
  for (i = 0; i < numpcs; i++)
    if (pt[i] & 0x08) j++;
  if (j == 1) {
    for (i = 0; i < numpcs; i++)
      if (pt[i] & 0x08) break;
    last_b = i;
  }
#endif

  table_w = alloc_huge(2 * size);
  table_b = table_w + size;

  init_threads(1);
  init_tables();

  LOCK_INIT(tc_mutex);
  LOCK_INIT(stats_mutex);

  gettimeofday(&start_time, NULL);
  cur_time = start_time;

  for (i = 0; i < 8; i++)
    if (pcs[i] != pcs[i + 8]) break;
  symmetric = (i == 8);
  split = !symmetric;

  struct tb_handle *G, *H;
  G = H = NULL;
  if (!only_generate) {
    if (generate_wdl)
      G = create_tb(tablename, 1, 6);
    if (generate_dtz)
      H = create_tb(tablename, 0, 10);
  }

  ubyte *tb_table = NULL;
  ubyte best_w[MAX_PIECES];
  ubyte best_b[MAX_PIECES];
  int bestp_w, bestp_b;

  for (i = 0; i < MAX_STATS; i++)
    global_stats_w[i] = global_stats_b[i] = 0;

  if (G || H)
    init_permute_pawn(pcs, pt);

  for (file = 0; file < 4; file++) {
    printf("Generating the %c-file.\n", 'a' + file);

    for (i = 0; i < MAX_STATS; i++)
      total_stats_w[i] = total_stats_b[i] = 0;
    num_saves = 0;
    lw_ply = lcw_ply = lb_ply = lcb_ply = -1;

    memset(piv_idx, 0, 8 * 64);
    memset(piv_valid, 0, 64);
    for (j = 0; j < 6; j++) {
      piv_sq[j] = ((j + 1) << 3 | file) ^ pw[0];
      piv_idx[piv_sq[j]] = ((long64)j) << shift[0];
      piv_valid[piv_sq[j]] = 1;
    }

    for (i = 0; i < numpcs; i++)
      cursed_capt[i] = 0;
    cursed_pawn_capt_w = cursed_pawn_capt_b = 0;
    has_cursed_capts = 0;

    printf("Initialising broken positions.\n");
    run_threaded(calc_broken, work_g, 1);
    printf("Calculating white piece captures.\n");
    calc_captures_w();
    if (has_white_pawns) {
      printf("Calculating white pawn captures.\n");
#ifdef SUICIDE
      run_threaded(last_b >= 0 ? calc_last_pawn_capture_w : calc_pawn_captures_w, work_g, 1);
#else
      run_threaded(calc_pawn_captures_w, work_g, 1);
#endif
    }
    printf("Calculating black piece captures.\n");
    calc_captures_b();
    if (has_black_pawns) {
      printf("Calculating black pawn captures.\n");
#ifdef SUICIDE
      run_threaded(last_w >= 0 ? calc_last_pawn_capture_b : calc_pawn_captures_b, work_g, 1);
#else
      run_threaded(calc_pawn_captures_b, work_g, 1);
#endif
    }
    for (i = 0; i < numpcs; i++)
      if (cursed_capt[i]) {
	has_cursed_capts = 1;
	break;
      }
    if (cursed_pawn_capt_w || cursed_pawn_capt_b)
      has_cursed_capts = 1;
    printf("Calculating pawn table.\n");
    if (slice_threading_low)
      calc_pawn_table_threaded();
    else
      calc_pawn_table_unthreaded();

    begin = 0;
    collect_stats(work_g, 1, num_saves);

    FILE *F = stdout;
    fprintf(F, "########## %s - file %c ##########\n\n", tablename,
		'a' + file);
    print_stats(F, total_stats_w, 1);
    print_stats(F, total_stats_b, 0);
    print_longest(F);
    fprintf(F, "\n");

    if (G || H) {
      reset_piece_captures();
      if (cursed_pawn_capt_w) {
	printf("Resetting white cursed pawn captures.\n");
	run_threaded(reset_pawn_captures_w, work_g, 1);
      }
      if (cursed_pawn_capt_b) {
	printf("Resetting black cursed pawn captures.\n");
	run_threaded(reset_pawn_captures_b, work_g, 1);
      }

      tb_table = init_permute_file(pcs, file);

      ply_accurate_w = 0;
      if (total_stats_w[DRAW_RULE] || total_stats_b[STAT_MATE - DRAW_RULE])
	ply_accurate_w = 1;

      ply_accurate_b = 0;
      if (total_stats_b[DRAW_RULE] || total_stats_w[STAT_MATE - DRAW_RULE])
	ply_accurate_b = 1;
    }

    // wdl
    if (G) {
      test_closs(total_stats_w, table_w, to_fix_w);
      prepare_wdl_map(total_stats_w, v, ply_accurate_w, ply_accurate_b);
      printf("find optimal permutation for file wtm / wdl, file %c\n", 'a' + file);
      permute_pawn_wdl(tb_table, pcs, pt, table_w, best_w, file, v);
      printf("compressing data for wtm / wdl, file %c\n", 'a' + file);
      compress_tb(G, tb_table, best_w, minfreq, maxsymbols);

      if (!symmetric) {
	test_closs(total_stats_b, table_b, to_fix_b);
	prepare_wdl_map(total_stats_b, v, ply_accurate_b, ply_accurate_w);
	printf("find optimal permutation for file btm / wdl, file %c\n", 'a' + file);
	permute_pawn_wdl(tb_table, pcs, pt, table_b, best_b, file, v);
	printf("compressing data for btm / wdl, file %c\n", 'a' + file);
	compress_tb(G, tb_table, best_b, minfreq, maxsymbols);
      }
    }

    // dtz
    if (H) {
#if defined(REGULAR) || defined(ATOMIC)
      fix_closs();
#endif

      sort_values(total_stats_w, &map_w[file], 0, ply_accurate_w, ply_accurate_b);
      if (!symmetric)
	sort_values(total_stats_b, &map_b[file], 1, ply_accurate_b, ply_accurate_w);

      if (num_saves > 0) {
	reconstruct_table(table_w, 'w', &map_w[file]);
	if (!symmetric)
	  reconstruct_table(table_b, 'b', &map_b[file]);
      }

      long64 estimate_w, estimate_b;

      prepare_dtz_map(v, &map_w[file]);
      printf("find optimal permutation for wtm / dtz, file %c\n", 'a' + file);
      estimate_w = estimate_pawn_dtz(pcs, pt, table_w, best_w, &bestp_w, file, v);
      if (!symmetric) {
	prepare_dtz_map(v, &map_b[file]);
	printf("find optimal permutation for btm / dtz, file %c\n", 'a' + file);
	estimate_b = estimate_pawn_dtz(pcs, pt, table_b, best_b, &bestp_b, file, v);
      } else
	estimate_b = UINT64_MAX;

      if (estimate_w <= estimate_b) {
	prepare_dtz_map(v, &map_w[file]);
	printf("permute table for wtm / dtz, file %c\n", 'a' + file);
	permute_pawn_dtz(tb_table, pcs, table_w, bestp_w, file, v);
	printf("compressing data for wtm/dtz, file %c\n", 'a' + file);
	compress_tb(H, tb_table, best_w, minfreq, maxsymbols);
      } else {
	prepare_dtz_map(v, &map_b[file]);
	printf("permute table for btm / dtz, file %c\n", 'a' + file);
	permute_pawn_dtz(tb_table, pcs, table_b, bestp_b, file, v);
	printf("compressing data for btm/dtz, file %c\n", 'a' + file);
	compress_tb(H, tb_table, best_b, minfreq, maxsymbols);
      }
    }

    for (i = 0; i < MAX_STATS; i++) {
      global_stats_w[i] += total_stats_w[i];
      global_stats_b[i] += total_stats_b[i];
    }

    free(tb_table);
  }

  if (G) merge_tb(G);
  if (H) merge_tb(H);

  printf("########## %s ##########\n\n", tablename);
  print_stats(stdout, global_stats_w, 1);
  print_stats(stdout, global_stats_b, 0);
  print_global_longest(stdout);

  if (save_stats) {
    FILE *F;
    char filename[128];
    char *dirptr = getenv(STATSDIR);
    if (dirptr && strlen(dirptr) < 100)
      strcpy(filename, dirptr);
    else
      strcpy(filename, ".");
    strcat(filename, "/");
    strcat(filename, tablename);
    strcat(filename, ".txt");
    F = fopen(filename, "w");
    if (F) {
      fprintf(F, "########## %s ##########\n\n", tablename);
      print_stats(F, global_stats_w, 1);
      print_stats(F, global_stats_b, 0);
      print_global_longest(F);
      fclose(F);
    }
  }

  return 0;
}
