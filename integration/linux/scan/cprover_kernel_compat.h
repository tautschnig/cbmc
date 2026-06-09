/* CPROVER/goto-cc kernel compatibility shim.
 *
 * Force-included (-include) BEFORE the kernel's own headers when building
 * real kernel objects with goto-cc, to neutralise GCC constructs the
 * CBMC C front-end does not model.  Paired with stripping -Werror (the
 * kernel escalates incompatible-pointer-types etc. to errors; goto-cc
 * only needs them as warnings).
 *
 * Each entry is a deliberate, documented over-approximation that is
 * sound for bounded memory-safety verification of parser logic (it drops
 * annotations that do not affect the values/sizes CBMC reasons about).
 */
#ifndef CPROVER_KERNEL_COMPAT_H
#define CPROVER_KERNEL_COMPAT_H

/* GCC attributes the CBMC front-end does not recognise.  These are pure
 * annotations (no effect on semantics CBMC cares about), so empty them.
 * We define the kernel's __-wrapped macros; because compiler_attributes.h
 * re-#defines them, we instead pre-empt via the attribute keyword itself
 * where possible.  The reliable lever is the kernel macro names, applied
 * by also passing -U/-D on the command line for the few that re-define. */
#define __nonstring
#define __counted_by(x)
#define __counted_by_le(x)
#define __counted_by_be(x)

#endif /* CPROVER_KERNEL_COMPAT_H */
