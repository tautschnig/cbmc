// Controlled ground-truth example for the CodeQL->CBMC
// two-stage refinement prototype.
//
// Both functions take an untrusted length and memcpy into a
// 64-byte buffer.  copy_vuln checks against the WRONG bound
// (128) and is a real OOB write; copy_fixed checks against
// the actual buffer size and is safe.
//
// Stage 1 (CodeQL, over-approximating taint) flags BOTH:
//   tainted length reaches a memcpy size in each.
// Stage 2 (CBMC, precise) must separate them:
//   copy_vuln  -> counterexample (witness length)
//   copy_fixed -> proved safe (CodeQL FP filtered).
#include <string.h>
#include <stdint.h>

// Untrusted source (model of copy_from_user/get_user/ioctl
// arg).  Nondet under CBMC; plain declaration for CodeQL.
#ifdef CBMC_HARNESS
uint32_t untrusted_len(void)
{
	uint32_t x; // uninitialised => nondet under CBMC
	return x;
}
#else
uint32_t untrusted_len(void);
#endif

static char dst[64];

void copy_vuln(const char *src)
{
	uint32_t len = untrusted_len();
	if(len > 128)            // WRONG bound: dst is only 64
		return;
	memcpy(dst, src, len);   // OOB write for 64 < len <= 128
}

void copy_fixed(const char *src)
{
	uint32_t len = untrusted_len();
	if(len > sizeof(dst))    // correct bound
		return;
	memcpy(dst, src, len);
}

#ifdef CBMC_HARNESS
// Stage-2 harnesses: provide a readable source buffer big
// enough that the only possible failure is the dst write,
// then let CBMC's default --bounds-check decide.
static char src_buf[128];

void harness_vuln(void)
{
	copy_vuln(src_buf);
}

void harness_fixed(void)
{
	copy_fixed(src_buf);
}
#endif
