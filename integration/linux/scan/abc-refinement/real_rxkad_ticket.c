// Real-function reach harness for the bounded-cursor candidate
// rxkad_decrypt_ticket (net/rxrpc/rxkad.c), flagged by
// skb_field_before_lencheck.ql (bounded-cursor branch).
//
// The verbatim memory-safety core of the ticket parse: after the in-place,
// length-preserving skcipher decrypt (modelled away -- the bytes are
// attacker-controlled regardless), the function reads a flags byte and
// walks NUL-terminated name fields bounded by `end`, via the verbatim Z()
// macro from rxkad.c.  memchr/isprint/abort are stubbed to their real
// byte-level semantics.
//
// Triage point: the oracle flags this at FUNCTION granularity ("reads a
// field before a length check in the function").  The flags read `*p`
// indeed has no in-function guard -- but the CALLER
// (rxkad_verify_response, rxkad.c:1167) rejects ticket_len < 4.  So the
// apparent bug is an FP closed by the caller's precondition -- which the
// verbatim+precondition harness demonstrates: probe_vuln (function in
// isolation, ticket_len unconstrained) is OOB-reachable; probe_fixed (with
// the real caller guard ticket_len >= 4) is safe.
#include "cover_probe.h"
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;

#define ANAME_SZ 40
#define INST_SZ 40
#define REALM_SZ 40

// real-semantics stubs for the externals the parse uses
static void *kmemchr(const void *s, int c, unsigned long n)
{
  const u8 *pp = s;
  for(unsigned long i = 0; i < n; i++)
    if(pp[i] == (u8)c)
      return (void *)(pp + i);
  return 0;
}
static int kisprint(int c)
{
  return c >= 0x20 && c < 0x7f;
}
static int abort_conn(void)
{
  return -1;
}

// ---- VERBATIM bounded-cursor parse core (rxkad_decrypt_ticket) ----------
static int rxkad_parse_ticket(void *ticket, unsigned long ticket_len)
{
  u8 *p, *q, *name, *end;
  int little_endian;
  u8 addr[4], key[8];

  p = ticket;
  end = p + ticket_len;

#define Z(field_SZ, lbl)                                                       \
  ({                                                                           \
    u8 *__str = p;                                                             \
    q = kmemchr(p, 0, end - p);                                                \
    if(!q || q - p > field_SZ)                                                 \
      return abort_conn();                                                     \
    for(; p < q; p++)                                                          \
      if(!kisprint(*p))                                                        \
        return abort_conn();                                                   \
    p++;                                                                       \
    __str;                                                                     \
  })

  CHECKPOINT(oob, ticket_len < 1); // flags-read OOB precondition
  little_endian = *p & 1;          // OOB read when ticket_len == 0
  p++;

  name = Z(ANAME_SZ, aname);
  name = Z(INST_SZ, inst);
  name = Z(REALM_SZ, realm);

  if(end - p < 4 + 8 + 4 + 2)
    return abort_conn();
  for(int k = 0; k < 4; k++) // memcpy(&addr, p, 4)
    addr[k] = *p++;
  for(int k = 0; k < 8; k++) // memcpy(&key, p, 8)
    key[k] = *p++;

  (void)little_endian;
  (void)name;
  (void)addr;
  (void)key;
  return 0;
}

unsigned long ndl(void)
{
  unsigned long x;
  return x;
}

// vulnerable in isolation: ticket_len unconstrained (including 0).
int probe_vuln(void)
{
  unsigned long tl = ndl();
  __CPROVER_assume(tl <= 8); // bound for tractability
  u8 *t = malloc(tl);
  __CPROVER_assume(tl == 0 || t != 0);
  return rxkad_parse_ticket(t, tl);
}

// fixed: the REAL caller guard (rxkad.c:1167 rejects ticket_len < 4).
int probe_fixed(void)
{
  unsigned long tl = ndl();
  __CPROVER_assume(tl >= 4 && tl <= 8);
  u8 *t = malloc(tl);
  __CPROVER_assume(t != 0);
  return rxkad_parse_ticket(t, tl);
}
