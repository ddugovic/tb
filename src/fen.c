/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef FEN_H
#define FEN_H

static char pc[] = { 0, 'P', 'N', 'B', 'R', 'Q', 'K', 0, 0, 'p', 'n', 'b', 'r', 'q', 'k', 0};

static void print_fen(FILE *F, long64 idx, int wtm, int switched)
{
  long64 idx2 = idx;
  int i, j;
  int n = numpcs;
  int p[MAX_PIECES];
  ubyte bd[64];

  memset(bd, 0, 64);

#ifndef SMALL
  for (i = n - 1; i > 0; i--, idx2 >>= 6)
    bd[p[i] = idx2 & 0x3f] = i + 1;
  bd[p[0] = inv_tri0x40[idx2]] = 1;
#else
  for (i = n - 1; i > 1; i--, idx2 >>= 6)
    bd[p[i] = idx2 & 0x3f] = i + 1;
  bd[p[0] = KK_inv[idx2][0]] = 1;
  bd[p[1] = KK_inv[idx2][1]] = 2;
#endif

  for (i = 56; i >= 0; i -= 8) {
    int cnt = 0;
    for (j = i; j < i + 8; j++)
      if (!bd[j])
        cnt++;
      else {
        if (cnt > 0) {
          fprintf(F, "%c", '0' + cnt);
          cnt = 0;
        }
        fprintf(F, "%c", pc[pt[bd[j]-1] ^ switched]);
      }
    if (cnt)
      fprintf(F, "%c", '0' + cnt);
    if (i) fprintf(F, "/");
  }
  fprintf(F, " %c - -\n", wtm ? 'w' : 'b');
}
#endif
