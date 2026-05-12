/* scan-compat.h — small set of macro overrides that let
 * scan/compile_file.sh build recent-LTS kernel TUs through
 * goto-cc without patching the kernel tree.
 *
 * These are goto-cc-only workarounds.  Each is safe under the
 * conservative "we are building a goto binary, not machine code"
 * interpretation: the overridden macro always expands to a
 * sound-but-loose value (zero / no-op), so the kernel's
 * semantic behaviour is unchanged; only some compile-time
 * correctness checks are disabled.
 *
 * Scope: included via `-include` by scan/compile_file.sh AFTER
 * the kernel's own `-include` headers but before any kernel
 * source-file include directive, so it can `#undef` / redefine
 * whatever earlier headers have already installed.
 *
 * Kernel versions:
 *   - Linux 5.10:  these overrides are no-ops (the macros either
 *                  didn't exist or already expanded to zero).
 *   - Linux 6.6+:  required to dodge the `__is_constexpr` /
 *                  `__builtin_choose_expr` constant-folding
 *                  pathology that CBMC's goto-cc front-end
 *                  mis-evaluates (see LIM-014 in
 *                  integration/linux/CBMC_LIMITATIONS.md).
 */

#ifndef INTEGRATION_LINUX_SCAN_FRAGMENTS_SCAN_COMPAT_H
#define INTEGRATION_LINUX_SCAN_FRAGMENTS_SCAN_COMPAT_H

/* LIM-014 workaround.  GENMASK_INPUT_CHECK on 6.x expands to a
 * BUILD_BUG_ON_ZERO wrapping __builtin_choose_expr / __is_constexpr.
 * goto-cc sometimes picks the wrong __builtin_choose_expr branch
 * and then complains the result is not a constant expression.
 * Short-circuit to 0 (the assembly fallback the kernel itself
 * uses when __ASSEMBLY__ is set).
 *
 * We have to pull in the kernel's bits.h FIRST so its own
 * `#define GENMASK_INPUT_CHECK` has run, and only THEN override
 * it — otherwise our override would fire before bits.h and get
 * overwritten when bits.h is later re-processed.  bits.h's
 * header guard protects against the second include.
 */
#include <linux/bits.h>
#ifdef GENMASK_INPUT_CHECK
#  undef GENMASK_INPUT_CHECK
#endif
#define GENMASK_INPUT_CHECK(h, l) 0

/* Same root cause, different macro.  __is_constexpr is defined
 * in <linux/const.h>, which is included transitively by many
 * headers.  Pull it in explicitly for the same "override-after-
 * definition" reason. */
#include <linux/const.h>
#ifdef __is_constexpr
#  undef __is_constexpr
#endif
#define __is_constexpr(x) 0

/* Another goto-cc constant-folding pathology on 6.x.  The kernel's
 * __cacheline_group_begin_aligned macro expands to
 *   __aligned((__VA_ARGS__ + 0) ? : SMP_CACHE_BYTES)
 * which uses GCC's `?:` with missing middle operand.  CBMC's
 * front-end doesn't constant-fold this inside an alignment
 * attribute and aborts with "expected constant expression".
 * Replace with a plain SMP_CACHE_BYTES alignment — sound because
 * we're building goto binaries, not machine code, and the
 * alignment attribute is irrelevant to CBMC's semantic reasoning. */
#include <linux/cache.h>
#ifdef __cacheline_group_begin_aligned
#  undef __cacheline_group_begin_aligned
#endif
#define __cacheline_group_begin_aligned(GROUP, ...)                            \
  __cacheline_group_begin(GROUP) __aligned(SMP_CACHE_BYTES)

#ifdef __cacheline_group_end_aligned
#  undef __cacheline_group_end_aligned
#endif
#define __cacheline_group_end_aligned(GROUP) __cacheline_group_end(GROUP)

/* Linux 6.12 introduced a family of compile-time string-buffer
 * checks built around `__builtin_has_attribute(expr, nonstring)`.
 * CBMC's ansi-c front-end parses __builtin_has_attribute but
 * then tries to look up the attribute name ('nonstring') as an
 * ordinary identifier and fails.  Override __must_be_cstr to 0
 * (skipping the check) and __must_be_array to 0 as well for
 * symmetry — both are BUILD_BUG_ON_ZERO wrappers that encode
 * GCC-builtin probes.  Sound for goto-cc scans: the kernel
 * semantics are unchanged; only the compile-time attribute
 * probes are dropped. */
#include <linux/compiler.h>
#ifdef __must_be_cstr
#  undef __must_be_cstr
#endif
#define __must_be_cstr(p) 0
#ifdef __must_be_array
#  undef __must_be_array
#endif
#define __must_be_array(a) 0
#ifdef __annotated
#  undef __annotated
#endif
#define __annotated(p, attr) 0

#endif
