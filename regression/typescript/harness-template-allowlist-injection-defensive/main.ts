// =====================================================================
// Harness Template: Allowlist Injection (DEFENSIVE form)
// =====================================================================
//
// **When to use**:
//   The function under audit looks up an attacker-influenceable
//   string in an allowlist and does something privileged with the
//   matching entry. The allowlist is implemented as a
//   string-keyed object literal (NOT a Set or Map) — so a `key in
//   allowlist` check would walk the prototype chain.
//
//   Threat model: the application caller has already constrained
//   `key` to be one of the legitimate names. We're verifying that
//   the resulting value passes __CPROVER_assert_in_allowlist.
//
// **What this verifies**:
//   That __CPROVER_assert_in_allowlist holds for the constrained
//   nondet input.
//
// **Customise**:
//   - Replace ALLOWED with the real allowlist constants.
//   - Inline the lookup logic into harness() (avoid a function-call
//     boundary; symbolic-string precision degrades through calls
//     per §1.3 of the limitations doc).
//   - The harness's nondet source should match the actual caller's
//     input shape.
//
// See also: `harness-template-allowlist-injection-buggy/` to
// confirm the harness shape exercises the bug pattern.

function harness(): void {
    const ALLOWED: string[] = ["create", "update", "delete"];
    const action = nondet_string();
    // Application contract: caller has already validated `action` is
    // one of the allowed names. The model's symbolic-string solver
    // is precise on a single-value assume; for a multi-value
    // contract, repeat the harness once per allowed value.
    __CPROVER_assume(action === "create");
    __CPROVER_assert_in_allowlist(action, ALLOWED);
}
harness();
