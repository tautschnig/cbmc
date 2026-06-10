// Integrity / control-flow-integrity threat template (A3).  CBMC obligation:
//   asset   = control-flow integrity (an indirect-call target is legitimate)
//   actor   = attacker who can influence a function-pointer field's value
//   threat  = the function pointer is set outside the legitimate target set
//             (control-flow hijack when it is later called)
//   mitigation to prove = after the write, the pointer is in {legit targets}
#include <stdint.h>

typedef void (*op_t)(void);

static void good_a(void)
{
}
static void good_b(void)
{
}

struct obj
{
  op_t fn;
};

unsigned long nd(void)
{
  unsigned long x;
  return x;
}

// BUGGY: an attacker-influenced value is written straight into the
// function pointer -> arbitrary indirect-call target.
void set_buggy(void)
{
  struct obj o;
  unsigned long v = nd();
  o.fn = (op_t)v;
  __CPROVER_assert(o.fn == good_a || o.fn == good_b,
                   "function pointer within the legitimate target set");
}

// FIXED: select via bounded, explicit branches -> always a legit target.
void set_fixed(void)
{
  struct obj o;
  unsigned long i = nd();
  if(i == 0)
    o.fn = good_a;
  else if(i == 1)
    o.fn = good_b;
  else
    return;
  __CPROVER_assert(o.fn == good_a || o.fn == good_b,
                   "function pointer within the legitimate target set");
}
