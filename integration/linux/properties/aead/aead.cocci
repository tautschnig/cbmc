// @@
//   SmPL rule: aead.cocci
//
//   Coccinelle prefilter for the aead property module.  Flags every
//   function that calls aead_request_set_crypt.  The set of kernel
//   files with such a call is small (crypto/algif_aead.c and a
//   handful of in-kernel users of the AEAD API), so a prefilter hit
//   justifies spending CBMC time on the file; a miss means there is
//   no aead-API call site to analyse and the CBMC aead scan can be
//   skipped.
//
//   The rule is intentionally coarse.  Precision is CBMC's job: when
//   spatch reports a hit, run
//       properties/aead/*.c + scatterlist/*.c + page_provenance/*.c
//   linked with the kernel goto binary and apply
//       goto-instrument --replace-call-with-contract aead_request_set_crypt
//   to check sgl_all_user_writable(dst) at the call site.
//
//   Run with:
//       spatch --sp-file integration/linux/properties/aead/aead.cocci \
//              crypto/algif_aead.c
//
//   Expected output on Linux 5.10 `crypto/algif_aead.c`: one match.
//
//   TODO(M4b): extend with a stricter variant that flags only calls
//   preceded by sg_chain() on the destination, once the kernel
//   adapter lands and allows us to use the stricter filter as a
//   precondition for the more expensive CBMC run.
// @@

@ aead_call @
expression req, src, dst, len, iv;
position p;
@@

aead_request_set_crypt@p(req, src, dst, len, iv)

@ script:python collect @
p << aead_call.p;
@@

coccilib.report.print_report(p[0],
    "aead: aead_request_set_crypt call site — candidate for CBMC "
    "property scan (Copy Fail / CVE-2026-31431 bug class)")
