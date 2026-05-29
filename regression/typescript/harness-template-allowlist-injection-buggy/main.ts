// =====================================================================
// Harness Template: Allowlist Injection (BUGGY form)
// =====================================================================
//
// **When to use**:
//   You suspect the caller of an allowlist-checked lookup is
//   missing the upstream validator, and want to prove it. The
//   harness passes a known attack value ("constructor") directly
//   so CBMC can constant-fold the assertion to FAILURE.
//
// **What this verifies**:
//   That __CPROVER_assert_in_allowlist correctly rejects
//   "constructor" (a property of Object.prototype) when the
//   allowlist does not contain it. CBMC reports VERIFICATION FAILED.
//
// **Adapt to your audit**: replace "constructor" with any other
// likely-attack value (e.g., "__proto__", "prototype", or any
// string the upstream validator might let slip through).
//
// See also: `harness-template-allowlist-injection-defensive/` for
// the form your fix should converge on.

function harness(): void {
    const ALLOWED: string[] = ["create", "update", "delete"];
    // Direct attack: pass the known-bad value. The assertion fails
    // because "constructor" is not in ALLOWED but a naive `key in
    // obj` allowlist would have admitted it via prototype chain.
    __CPROVER_assert_in_allowlist("constructor", ALLOWED);
}
harness();
