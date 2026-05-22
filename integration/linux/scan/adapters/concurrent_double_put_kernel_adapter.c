/// \file
/// concurrent_double_put_kernel_adapter.c — adapter contracts.
///
/// Two contracts:
///
///   __cdp_get   — assigns + ensures __cdp_live = 1.
///   __cdp_put   — requires __cdp_live == 1; assigns +
///                 ensures __cdp_live = 0.
///
/// Unlike the per-pointer balance modules (which leave ghost
/// mutation to the harness), this module's contracts MUTATE
/// the ghost via assigns + ensures so that CBMC's concurrent
/// symex sees a sequentially-consistent decrement when one
/// thread's put completes.  Without that, two concurrent puts
/// would both pass their precondition (the ghost would stay
/// at 1 after the first put) and the bug shape would never
/// be detected.

extern unsigned int __cdp_live;

void __cdp_get(void) __CPROVER_assigns(__cdp_live)
  __CPROVER_ensures(__cdp_live == 1u);

void __cdp_put(void) __CPROVER_requires(__cdp_live == 1u)
  __CPROVER_assigns(__cdp_live) __CPROVER_ensures(__cdp_live == 0u);
