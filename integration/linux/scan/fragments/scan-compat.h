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

/* __is_constexpr used to need an override here because CBMC's
 * conditional-operator typechecking simplified `(long)x * 0L` to
 * 0 before deciding null-pointer-constant-ness, treating
 * runtime `x` as constant.  Fixed upstream in c_typecheck_expr
 * .cpp's typecheck_expr_trinary: we now pre-check that the
 * original (pre-simplification) operand is free of
 * non-constant leaves before treating it as a null pointer
 * constant.  No scan-compat override needed any more.
 *
 * Kept as a comment rather than a live #define so future
 * regressions on this idiom are easy to diagnose: if
 * __is_constexpr starts misbehaving again, re-adding the
 * `#define __is_constexpr(x) 0` line is a one-liner workaround.
 */

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

/* Linux 6.12's printk_ratelimited expands to a statement_expression
 * containing `static DEFINE_RATELIMIT_STATE(...)` — a static local
 * with a compound-literal initializer that includes a spinlock init.
 * CBMC's front-end requires static-local initialisers to be constant
 * expressions, but the spinlock init is not foldable.  Since printk
 * has no semantic effect on any property we check (it's a logging
 * side-effect), override to a no-op.  Sound for goto-cc scans. */
#include <linux/printk.h>
#ifdef printk_ratelimited
#  undef printk_ratelimited
#endif
#define printk_ratelimited(fmt, ...) do { } while (0)

/* Linux 6.1+ added `bpf_jit_fill_hole_with_zero` as a callback
 * parameter to `bpf_prog_pack_alloc` from kernel/bpf/dispatcher.c.
 * It's declared in <linux/filter.h> as an extern; its definition
 * lives in arch-specific code (e.g. arch/x86/net/bpf_jit_comp.c)
 * that isn't on the x86 allnoconfig scan path.  Without a body,
 * goto-cc refuses to take the function pointer: `failed to find
 * symbol bpf_jit_fill_hole_with_zero`.  Provide a weak no-op
 * definition so the reference resolves in every kernel TU.  Weak
 * linkage means the kernel's real definition wins whenever the
 * arch TU is on the link path.  Sound for goto-cc scans: the fill
 * behaviour only affects unallocated bytes inside an image buffer
 * and has no effect on any property we check.
 *
 * Declared locally rather than by `#include <linux/filter.h>`:
 * including filter.h transitively pulls in <linux/scatterlist.h>
 * and the rest of the kernel networking / bpf header graph, which
 * conflicts with the aead direct-call harness's own minimal
 * scatterlist declarations.  A bare function declaration is
 * enough for the weak definition below to be well-typed against
 * any call site that does #include <linux/filter.h>.
 */
__attribute__((weak))
void bpf_jit_fill_hole_with_zero(void *area, unsigned int size);
__attribute__((weak))
void bpf_jit_fill_hole_with_zero(void *area, unsigned int size)
{
  (void)area;
  (void)size;
}
