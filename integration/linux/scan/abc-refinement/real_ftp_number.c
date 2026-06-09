// Real-function reach harness for try_rfc959 / try_number
// (net/netfilter/nf_conntrack_ftp.c), flagged by the bounded-cursor
// oracle.  The actual cursor read lives in the verbatim try_number: the
// FTP PORT-command parser walks `data` bounded by `dlen` filling a
// fixed-size array[].  This is a TRUE-NEGATIVE demonstration: the loop is
// bounded by dlen and the array index by array_size, so CBMC clears it --
// the pipeline can DISMISS an oracle hit on real code, not only confirm
// one.  (probe_fixed is identical; there is nothing to fix.)
#include "cover_probe.h"
#include <stdint.h>
#include <stdlib.h>

typedef uint32_t u_int32_t;

// ---- VERBATIM (net/netfilter/nf_conntrack_ftp.c) ----
static int try_number(const char *data, size_t dlen, u_int32_t array[],
                      int array_size, char sep, char term)
{
  u_int32_t i, len;

  __builtin_memset(array, 0, sizeof(array[0]) * array_size);

  /* Keep data pointing at next char. */
  for(i = 0, len = 0; len < dlen && i < array_size; len++, data++)
  {
    if(*data >= '0' && *data <= '9')
    {
      array[i] = array[i] * 10 + *data - '0';
      if(array[i] > 255)
        return 0;
    }
    else if(*data == sep)
      i++;
    else
    {
      if((*data == term || !term) && i == array_size - 1)
        return len;
      return 0;
    }
  }
  return 0;
}

unsigned nd(void)
{
  unsigned x;
  return x;
}

// the parser over a (data, dlen) buffer; CBMC must show no OOB read of
// `data` for any dlen up to the modelled bound.
static int run(void)
{
  unsigned dlen = nd();
  __CPROVER_assume(dlen <= 8);
  char *data = (char *)malloc(dlen);
  __CPROVER_assume(dlen == 0 || data != 0);
  u_int32_t array[6];
  CHECKPOINT(oob, 0); // no OOB precondition exists -> always BLOCKED
  return try_number(data, dlen, array, 6, ',', '\n');
}

int probe_vuln(void)
{
  return run();
}
int probe_fixed(void)
{
  return run();
}
