/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static char pc[] = { 0, 'P', 'N', 'B', 'R', 'Q', 'K', 0, 0, 'p', 'n', 'b', 'r', 'q', 'k', 0};

static char fen_buf[128];

void print_fen(FILE *F, long64 idx, int wtm)
{
  long64 idx2 = idx ^ pw_mask;
  int i, j;
  int n = numpcs;
  int p[MAX_PIECES];
  ubyte bd[64];
  char *str = fen_buf;

  memset(bd, 0, 64);

  for (i = n - 1; i > 0; i--, idx2 >>= 6)
    bd[p[i] = idx2 & 0x3f] = i + 1;
  bd[p[0] = piv_sq[idx2]] = 1;

  for (i = 56; i >= 0; i -= 8) {
    int cnt = 0;
    for (j = i; j < i + 8; j++)
      if (!bd[j])
        cnt++;
      else {
        if (cnt > 0) {
	  *str++ = '0' + cnt;
          cnt = 0;
        }
	*str++ = pc[pt[bd[j]-1]];
      }
    if (cnt) *str++ = '0' + cnt;
    if (i) *str++ = '/';
  }
  *str++ = ' ';
  *str++ = wtm ? 'w' : 'b';
  *str++ = ' ';
  *str++ = '-';
  *str++ = ' ';
  *str++ = '-';
  *str++ = '\n';
  *str = 0;
  fputs(fen_buf, F);
}
