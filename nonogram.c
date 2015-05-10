/* Copyright © 2003-2014 Jakub Wilk <jwilk@jwilk.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "autoconfig.h"

#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <assert.h>
#include <math.h>
#ifdef HAVE_SIGACTION
#include <signal.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "config.h"
#include "memory.h"
#include "nonogram.h"
#include "queue.h"
#include "term.h"

Picture *mainpicture;
unsigned int *leftborder, *topborder;
uint64_t *gtestfield;
unsigned int xsize, ysize, xysize, xpysize, vsize;
unsigned int lmax, tmax;

uint64_t fingercounter;

static double binomln(int n, int k)
// Return
//   ln binom(n, k)
// or +0.0
{
  double tmp;

  if (n <= k || n <= 0 || k <= 0)
    return 0.0;

  double dn = (double)n;
  double dk = (double)k;

  tmp = -0.5 * log(8 * atan(1)); // atan(1) = π/4
  tmp += (dn + 0.5) * log(dn);
  tmp -= (dk + 0.5) * log(dk);
  tmp -= (dn - dk + 0.5) * log(dn - dk);
  return tmp;
}

static void raise_input_error(unsigned int n)
{
  fprintf(stderr, "Invalid input at line %u!\n", n);
  exit(EXIT_FAILURE);
}

static void handle_sigint()
{
  const char *reset_colors = term_strings.dark;
  if (reset_colors == NULL)
    reset_colors = "";
  fflush(stdout);
  fprintf(stderr, "%s\nOuch!\n\n", reset_colors);
  exit(EXIT_FAILURE);
}

static void setup_sigint()
{
#ifdef HAVE_SIGACTION
  struct sigaction act;
  act.sa_handler = handle_sigint;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  sigaction(SIGINT, &act, NULL);
#endif
}

static void print_picture_plain(bit *picture, bit *cpicture, bool use_ncurses)
{
  char *str_color;

  if (ENABLE_DEBUG && cpicture == NULL)
    cpicture = picture;

  unsigned int i, j, t;

  setup_termstrings(use_ncurses, config.utf8, config.color);

  printf("%s", term_strings.init);

  for (i = 0; i < tmax; i++)
  {
    printf(" ");
    for (j = 0; j < lmax; j++)
      printf("  ");
    for (j = 0; j < xsize; j++)
    {
      str_color = term_strings.light[j & 1];
      t = topborder[j * ysize + i];
      printf("%s", str_color);
      if (t != 0 || i == 0)
        printf("%2u", t);
      else
        printf("  ");
      printf("%s", term_strings.dark);
    }
    printf("\n");
  }

  for (i = 0; i < lmax; i++)
    printf("%s", "  ");
  printf("%s", term_strings.tl);
  for (i = 0; i < xsize; i++)
    printf("%s", term_strings.h);
  printf("%s", term_strings.tr);
  printf("%s", "\n");
  for (i = 0; i < ysize; i++)
  {
    for (j = 0; j < lmax; j++)
    {
      str_color = term_strings.light[j & 1];
      t = leftborder[i * xsize + j];
      printf("%s", str_color);
      if (t != 0 || j == 0)
        printf("%2u", t);
      else
        printf("%s", "  ");
      printf("%s", term_strings.dark);
    }
    printf("%s", term_strings.v);
    for (j = 0; j < xsize; j++)
    {
      str_color = term_strings.light[j & 1];
      switch (*picture)
      {
        case Q:
          printf("%s<>%s", str_color, term_strings.dark
          );
          break;
        case O:
          if (ENABLE_DEBUG && *cpicture == X)
            printf("%s..", term_strings.error);
          else
            printf("%s  ", str_color);
          printf("%s", term_strings.dark);
          break;
        case X:
          if (ENABLE_DEBUG && *cpicture == O)
            printf("%s", term_strings.error);
          else
            printf("%s", str_color);
          printf("%s%s", term_strings.hash, term_strings.dark);
          break;
      }
      picture++;
      if (ENABLE_DEBUG)
        cpicture++;
    }
    printf("%s\n", term_strings.v);
  }
  for (i = 0; i < lmax; i++)
    printf("%s", "  ");
  printf("%s", term_strings.bl);
  for (i = 0; i < xsize; i++)
    printf("%s", term_strings.h);
  printf("%s\n\n", term_strings.br);
  fflush(stdout);
}

static void print_html_dtd(bool use_xhtml, bool need_charset)
{
  if (use_xhtml && need_charset)
    printf("<?xml version='1.0' encoding='ISO-8859-1'?>\n");
  printf(use_xhtml ?
    "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n" :
    "<!DOCTYPE html PUBLIC '-//W3C//DTD HTML 4.01//EN' 'http://www.w3.org/TR/html4/strict.dtd'>\n");
}

static void print_picture_html(bit *picture, bool use_xhtml)
{
  unsigned int i, j;
  print_html_dtd(use_xhtml, true);
  printf("%s%s%s",
    "<html>\n"
    "<head>\n"
    "<title>Nonogram solution</title>\n"
    "<meta http-equiv='Content-type' content='text/html; charset=ISO-8859-1'", (use_xhtml ? " /" : ""), ">\n"
    "<style type='text/css'>\n"
    "  table "  "{ border-collapse: collapse; } \n"
    "  td, th " "{ font: 8pt Arial, sans-serif; width: 11pt; height: 11pt; }\n"
    "  th "     "{ background-color: #fff; color: #000;"
                 " border: dotted 1px #888; }\n"
    "  th.empty  { border: none; }\n"
    "  td.x "   "{ background-color: #000; color: #000; }\n"
    "  td.v "   "{ background-color: #888; color: #f00; }\n"
    "  td "     "{ background-color: #eee; color: #000;"
                 " border: solid 1px #888; text-align: center; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<table border='0' cellpadding='0' cellspacing='0'>");

  unsigned int *top_desc_size = alloc(xsize * sizeof (unsigned int));
  for (i = 0; i < xsize; i++)
  {
    top_desc_size[i] = 0;
    for (j = 0; j < tmax; j++)
      if (topborder[i * ysize + j] == 0)
        break;
      else
        top_desc_size[i]++;
  }

  for (i = 0; i < tmax; i++)
  {
    printf("<tr>");
    if (i == 0)
      printf("<th class='empty' colspan='%u' rowspan='%u'>\xa0</th>", lmax, tmax);
    for (j = 0; j < xsize; j++)
    {
      if (i < tmax - top_desc_size[j])
        printf("<th>\xa0</th>");
      else
        printf("<th>%u</th>", topborder[j * ysize + i - tmax + top_desc_size[j]]);
    }
    printf("</tr>\n");
  }

  free(top_desc_size);

  for (i = 0; i < ysize; i++)
  {
    printf("<tr>");
    for (j = 0; j < lmax; j++)
      if (leftborder[i * xsize + j] == 0)
        break;
    for (; j < lmax; j++)
      printf("<th>\xa0</th>");
    for (j = 0; j < lmax; j++)
    {
      unsigned int t = leftborder[i * xsize + j];
      if (t != 0)
        printf("<th>%u</th>", t);
    }
    for (j = 0; j < xsize; j++, picture++)
    switch (*picture)
    {
    case Q:
      printf("<td class='v'>?</td>");
      break;
    case O:
      printf("<td>\xa0</td>");
      break;
    case X:
      printf("<td class='x'>#</td>");
      break;
    }
    printf("</tr>\n");
  }
  printf("</table>\n</body>\n</html>\n");
}

static inline void print_picture(bit *picture, bit *cpicture)
{
  if (config.stats)
    return; // XXX undocumented!
  if (config.html)
    print_picture_html(picture, config.xhtml);
  else
    print_picture_plain(picture, cpicture, true);
}

static uint64_t touch_line(bit *picture, unsigned int range, uint64_t *testfield, unsigned int *borderitem, bool vert)
{
  unsigned int i, j, k, count, sum, mul;
  uint64_t z, ink;
  bool ok;

  fingercounter++;

  sum = count = 0;
  mul = vert ? xsize : 1;

  for (i = 0; borderitem[i] > 0; i++)
  {
    count++;
    sum += borderitem[i];
  }

  if (sum + count > range + 1)
    return 0;

  k = borderitem[0];
  z = 0;
  if (count == 1)
  for (i = 0; i + k <= range; i++)
  {
    ok = true;
    for (j = 0; j < i; j++)
      if (picture[j * mul] == X)
      {
        ok = false;
        break;
      };
    if (!ok)
      break;
    for (j = i; j < i + k && ok; j++)
      if (picture[j * mul] == O)
        ok = false;
    for (j = i + k; j < range && ok; j++)
      if (picture[j * mul] == X)
        ok = false;
    if (ok)
    {
      for (j = i; j < i + k; j++)
        testfield[j] += 1;
      z++;
    }
  }
  else
  for (i = 0; i <= range - sum - count + 1; i++)
  {
    ok = true;
    for (j = 0; j < i; j++)
      if (picture[j * mul] == X)
      {
        ok = false;
        break;
      };
    if (!ok)
      break;
    for (j = i; j < i + k && ok; j++)
      if (picture[j * mul] == O)
        ok = false;
    if (!ok)
      continue;
    if (i + k < range && picture[(i + k) * mul] == X)
      continue;
    j = i + k + 1;
    ink =
      (count == 1) ?
        1 :
        touch_line(picture + j * mul, range - j, testfield + j, borderitem + 1, vert);
    if (ink != 0)
    {
      for (j = i; j < i + k; j++)
        testfield[j] += ink;
      z += ink;
    }
  }
  return z;
}

static void finger_line(Picture *mpicture, Queue *queue)
{
  bit *picture;
  uint64_t *testfield;
  uint64_t q, u;
  unsigned int i, j, imul, mul, size, oline, line;
  int factor;
  bool vert;
  //  int myid = __cilkrts_get_worker_number();

  fingercounter++;
  pthread_mutex_lock(&(queue->queue_lock));
  line = oline = get_from_queue(queue);
  if (line == -1) {
    pthread_mutex_unlock(&(queue->queue_lock));
    return;
  }
  //  pthread_mutex_unlock(&(queue->queue_lock));
  if (line < ysize)
    imul = xsize, mul = 1, size = xsize, vert = false;
  else
    imul = 1, mul = xsize, size = ysize, line -= ysize, vert = true;

  j = mpicture->linecounter[oline];
  if (j == 0 || j == size) {
    pthread_mutex_unlock(&(queue->queue_lock));
    return;
  }

  pthread_mutex_lock(&(mpicture->pic_lock));
  picture = mpicture->bits + line * imul;
  testfield = gtestfield;
  memset(testfield, 0, size * sizeof(uint64_t));

  if (vert)
    q = touch_line(picture, size, testfield, topborder + line * size, true);
  else
    q = touch_line(picture, size, testfield, leftborder + line * size, false);

  j = vert ? 0 : ysize;
  for (i = j; i < j + size; i++)
  {
    u = *testfield++;
    if ((u == q || u == 0) && (*picture == Q))
    {
      mpicture->counter--;
      mpicture->linecounter[oline]--;
      factor = MAX_FACTOR * (--mpicture->linecounter[i]) / size + mpicture->evilcounter[i];
      //      pthread_mutex_lock(&(queue->queue_lock));
      put_into_queue(queue, i, factor);
      *picture = u ? X : O;
    }
    picture += mul;
  }
      pthread_mutex_unlock(&(queue->queue_lock));
      pthread_mutex_unlock(&(mpicture->pic_lock));
}

static void finger_lines(Picture *mpicture, Queue *queue) {
  printf("%d workers\n", __cilkrts_get_nworkers());
  while (1) {
    if (is_queue_empty(queue))
      break;
    //to do: set grainsize, cilk spawn here
    cilk_spawn finger_line(mpicture, queue);
  }
}

static bool check_consistency(bit *picture)
{
  bool fr;
  unsigned int i, j;
  unsigned int r, rv;
  unsigned int *border;
  bit *tpicture;

  for (i = 0; i < ysize; i++)
  {
    fr = true;
    r = 0;
    rv = 0;
    border = leftborder + i * xsize;
    tpicture = picture + i * xsize;
    for (j = 0; j < xsize && fr; j++, tpicture++)
    switch (*tpicture)
    {
    case Q:
      fr = false;
      break;
    case X:
      rv++;
      break;
    case O:
      if (rv == 0)
        break;
      if (*border != rv)
      {
        if (ENABLE_DEBUG)
          fprintf(stderr, "Inconsistency at row #%u[%u]! (%u, expected %u)!\n", i, j, rv, *border);
        return false;
      }
      rv = 0; r++; border++;
      break;
    default:
      ;
    }
    if (fr && *border != rv)
    {
      if (ENABLE_DEBUG)
        fprintf(stderr, "Inconsistency at the end of row #%u! (%u, expected %u)\n", i, rv, *border);
      return false;
    }
  }

  for (i = 0; i < xsize; i++)
  {
    fr = true;
    r = 0;
    rv = 0;
    border = topborder + i * ysize;
    tpicture = picture + i;
    for (j = 0; j < ysize && fr; j++, tpicture += xsize)
    switch (*tpicture)
    {
    case Q:
      fr = false;
      break;
    case X:
      rv++;
      break;
    case O:
      if (rv == 0)
        break;
      if (*border != rv)
      {
        if (ENABLE_DEBUG)
          fprintf(stderr, "Inconsistency at column #%u[%u]! (%u, expected %u)\n", i, j, rv, *border);
        return false;
      }
      rv = 0; r++; border++;
      break;
    default:
      ;
    }
    if (fr && *border != rv)
    {
      if (ENABLE_DEBUG)
        fprintf(stderr, "Inconsistency at the end of column #%u! (%u, expected %u)\n", i, rv, *border);
      return false;
    }
  }
  return true;
}

static inline void *alloc_border(void)
{
  return alloc(vsize * sizeof(unsigned int));
}

static inline void *alloc_testfield(void)
{
  return alloc(xysize * sizeof(uint64_t));
}

static void *alloc_picture(void)
{
  unsigned int i;
  Picture *tmp =
    alloc(
      offsetof(Picture, bits) +
      vsize * sizeof(bit) );
  tmp->linecounter = alloc(sizeof(unsigned int) * xpysize);
  tmp->evilcounter = alloc(sizeof(unsigned int) * xpysize);
  pthread_mutex_init(&(tmp->pic_lock), NULL);
  for (i = 0; i < ysize; i++)
    tmp->linecounter[i] = xsize;
  for (i = 0; i < xsize; i++)
    tmp->linecounter[ysize + i] = ysize;
  tmp->counter = vsize;
  return tmp;
}

static inline void free_picture(Picture *picture)
{
  free(picture->linecounter);
  free(picture->evilcounter);
  free(picture);
}

static inline void duplicate_picture(Picture *src, Picture *dst)
{
  dst->counter = src->counter;
  memcpy(dst->linecounter, src->linecounter, sizeof(unsigned int) * xpysize);
  memcpy(dst->evilcounter, src->evilcounter, sizeof(unsigned int) * xpysize);
  memcpy(dst->bits, src->bits, vsize * sizeof(bit));
}

static void preliminary_shake(Picture *mpicture)
{
  unsigned int i, j, k;
  unsigned int R, ML;
  bit *picture;
  unsigned int *band;

  for (i = 0; i < ysize; i++)
  {
    band = leftborder + i * xsize;
    R = *band++;
    ML = R;
    while (*band > 0)
      ML += *band++ + 1;

    band = leftborder + i * xsize;
    if (*band == 0)
    {
      picture = &mpicture->bits[i * xsize];
      for (j = 0; j < xsize; j++, picture++)
      {
        *picture = O;
        mpicture->counter--;
      }
    }
    while (*band > 0)
    {
      k = xsize - ML;
      if (k < R)
      {
        picture = mpicture->bits + i * xsize + k;
        for ( ; k < R; k++, picture++)
        {
          *picture = X;
          mpicture->counter--;
        }
      }
      ML -= *band; ML--;
      R++; R += *++band;
    }
  }

  for (i = 0; i < xsize; i++)
  {
    band = topborder + i * ysize;
    R = *band++;
    ML = R;
    while (*band > 0)
      ML += *band++ + 1;

    band = topborder + i * ysize;
    if (*band == 0)
    {
      picture = mpicture->bits + i;
      for (j = 0; j < ysize; j++, picture += xsize)
      if (*picture == Q)
      {
        *picture = O;
        mpicture->counter--;
      }
    }
    while (*band > 0)
    {
      k = ysize - ML;
      if (k < R)
      {
        picture = mpicture->bits + k * xsize + i;
        for ( ; k < R; k++, picture += xsize)
        if (*picture == Q)
        {
          *picture = X;
          mpicture->counter--;
        }
      }
      ML -= *band; ML--;
      R++; R += *++band;
    }
  }

  picture = mpicture->bits;
  for (i = 0; i < ysize; i++)
  for (j = 0; j < xsize; j++, picture++)
  if (*picture != Q)
  {
    mpicture->linecounter[i]--;
    mpicture->linecounter[ysize + j]--;
  }
}

static inline void shake(Picture *mpicture)
{
  unsigned int i, j;
  int factor;
  Queue *queue = alloc_queue();
  clock_t fingerstart, fingerend, ticks;
  double seconds;

  assert(ysize > 0);

for (i = 0; i < ysize; i++)
  {
    factor = MAX_FACTOR * mpicture->linecounter[i] / xsize + mpicture->evilcounter[i];
    put_into_queue(queue, i, factor);
  }

  for (i = 0, j = ysize; i < xsize; i++, j++)
  {
    factor = MAX_FACTOR * mpicture->linecounter[j] / ysize + mpicture->evilcounter[i];
    put_into_queue(queue, j, factor);
  }

  fingerstart = clock();
  finger_lines(mpicture, queue);
  fingerend = clock();
  ticks = fingerend-fingerstart;
  seconds = (double) ((ticks + 0.0) / CLOCKS_PER_SEC);

  printf("fingering: %.02f seconds CPU\n", seconds);
  free_queue(queue);
  return;
}



static bool backtrack(Picture *mpicture)
{
  Picture *mclone;
  bit *picture;
  unsigned int i, j, n;
  bool res = false;

  mclone = alloc_picture();
  picture = mpicture->bits;
  n = 0;
  for (i = 0; i < ysize && !res; i++)
  for (j = 0; j < xsize && !res; j++, n++, picture++)
  if (*picture == Q)
  {
    duplicate_picture(mpicture, mclone); // mpicture --> mclone
    mclone->bits[n] = O;
    mclone->counter--;
    mclone->linecounter[i]--;
    mclone->linecounter[ysize + j]--;
    shake(mclone);
    res = backtrack(mclone);
    if (res)
      duplicate_picture(mclone, mpicture); // mclone --> mpicture
    else
    {
      *picture = X;
      mpicture->counter--;
      mpicture->linecounter[i]--;
      mpicture->linecounter[ysize + j]--;
      shake(mpicture);
    }
  }
  free_picture(mclone);
  return check_consistency(mpicture->bits);
}

static unsigned int measure_evil(int r, int k)
{
  double tmp = binomln(r, k);
  if (tmp > MAX_EVIL)
    tmp = MAX_EVIL;
  return floor(tmp * MAX_EVIL * MAX_FACTOR);
}

int main(int argc, char **argv)
{
  char c;
  int rc;
  unsigned int i, j, k, sane;
  unsigned int evs, evm;
  bit *checkbits = NULL;
  clock_t start, end, ticks;

#if ENABLE_DEBUG
  FILE *verifyfile;
  Picture *checkpicture;
#endif
  static char *verifyfname = NULL;

  setup_sigint();

  parse_arguments(argc, argv, &verifyfname);

  xsize = ysize = 0;
  c = readchar();
  while (c >= '0' && c <= '9')
  {
    xsize *= 10;
    xsize += c - '0';
    c = readchar();
  }
  while (c == ' ' || c == '\t')
    c = readchar();
  while (c >= '0' && c <= '9')
  {
    ysize *= 10;
    ysize += c - '0';
    c = readchar();
  }
  while (c <= ' ')
    c = readchar();

  if (xsize < 1 || ysize < 1 || xsize > MAX_SIZE || ysize > MAX_SIZE)
    raise_input_error(1);

  vsize = xsize * ysize;
  xpysize = xsize + ysize;
  xysize = xsize > ysize ? xsize : ysize; // max(xsize, ysize)

  leftborder = alloc_border();
  topborder = alloc_border();
  gtestfield = alloc_testfield();

  mainpicture = alloc_picture();

  evs = evm = 0;
  sane = (unsigned int) -1;
  lmax = 0;
  for (i = j = 0; i < ysize; )
  {
    k = 0;
    while (c >= '0' && c <= '9')
    {
      k *= 10;
      k += c-'0';
      c = readchar();
    }
    sane += k + 1;
    if ((sane>xsize) || (k == 0 && j > 0))
      raise_input_error(2 + i);
    leftborder[i * xsize + j] = k;
    evs += k;
    if (k > evm)
      evm = k;
    while (c == ' ' || c == '\t')
      c = readchar();
    if (c == '\r' || c == '\n' || c == '\0')
    {
      if (j > lmax)
        lmax = j;
      mainpicture->evilcounter[i] = measure_evil(xsize - evs + 1, j + 1);
      evs = evm = 0;
      i++;
      j = 0;
      sane = (unsigned int) -1;
      do
        c = readchar();
      while (c == '\r' || c == '\n');
    }
    else
      j++;
  }

  assert(sane == (unsigned int)-1);
  tmax = 0;
  for (i = j = 0; i < xsize; )
  {
    k = 0;
    while (c >= '0' && c <= '9')
    {
      k *= 10;
      k += c-'0';
      c = readchar();
    }
    sane += k + 1;
    if ((sane > ysize) || (k == 0 && j > 0))
      raise_input_error(2 + ysize + i);
    topborder[i * ysize + j] = k;
    evs += k;
    if (k > evm)
      evm = k;
    while (c == ' ' || c == '\t')
      c = readchar();
    if (c == '\r' || c == '\n' || c == '\0')
    {
      if (j > tmax)
        tmax = j;
      mainpicture->evilcounter[ysize + i] = measure_evil(ysize - evs + 1, j + 1);
      evs = evm = 0;
      i++;
      j = 0;
      sane = (unsigned int) -1;
      do
        c = readchar();
      while (c=='\r' || c=='\n');
    }
    else
      j++;
  }

  lmax++;
  tmax++;

#if ENABLE_DEBUG
  if (verifyfname != NULL)
  {
    verifyfile = fopen(verifyfname, "r");
    if (verifyfile != NULL)
    {
      checkpicture = alloc_picture();
      checkpicture->counter = 0;
      c = 0;
      for (i = 0; i < ysize; i++)
      {
        while (c < ' ')
          c = freadchar(verifyfile);
        for (j = 0; j < xsize; j++)
        {
          checkpicture->bits[i * xsize + j] = (c == '#') ? X : O;
          freadchar(verifyfile);
          c = freadchar(verifyfile);
        }
      }
      fclose(verifyfile);
      checkbits = checkpicture->bits;
    }
  }
#endif /* ENABLE_DEBUG */

  rc = EXIT_SUCCESS;

  fingercounter = 0;
  start = clock();

  preliminary_shake(mainpicture);
  shake(mainpicture);

  if (!check_consistency(mainpicture->bits))
  {
    fingercounter = 0;
    end = clock();
    ticks = end - start;

    rc = EXIT_FAILURE;
    fprintf(stderr, "Inconsistent puzzle!\n");
    if (ENABLE_DEBUG)
      print_picture(mainpicture->bits, checkbits);
  }
  else
  {
    if ((mainpicture->counter == 0) || ENABLE_DEBUG)
      print_picture(mainpicture->bits, checkbits);
    if (mainpicture->counter != 0)
    {
      fprintf(stderr,
        "Line solving failed (n=%u).\n"
        "Resorting to backtracking, but this may take a while...\n",
        mainpicture->counter
      );
      printf("backtracking\n");
      if (backtrack(mainpicture))
        print_picture(mainpicture->bits, checkbits);
      else
      {
        rc = EXIT_FAILURE;
        fingercounter = 0;
        fprintf(stderr, "Inconsistent puzzle!\n");
      }
    }
    end = clock();
    ticks = end-start;
  }

  printf("Processing time: %.2f sec\n", (double) ((ticks + 0.0)/ CLOCKS_PER_SEC));
  printf("%ju\n", fingercounter);

  return rc;
}

/* vim:set ts=2 sts=2 sw=2 et: */
