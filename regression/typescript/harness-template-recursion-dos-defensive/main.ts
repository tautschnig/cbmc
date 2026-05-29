// =====================================================================
// Harness Template: Recursion-DoS (DEFENSIVE form)
// =====================================================================
//
// **When to use**:
//   The function under audit is recursive AND its recursion depth is
//   driven by an external input (string length, array length, JSON
//   nesting, etc.) AND the input source is attacker-influenceable.
//
// **What this verifies**:
//   That the function refuses to recurse deeper than MAX_DEPTH for
//   any input the harness's nondet source generates. The
//   `__CPROVER_assume(input.length <= MAX_DEPTH)` documents the
//   caller-side contract and lets the assertion verify cleanly.
//
// **Customise**:
//   - Replace `process_under_audit` with the real function.
//   - Set MAX_DEPTH to your application's actual policy.
//   - Replace `nondet_string()` with the actual source shape the
//     function consumes (string, JSON-parsed object, array, etc.).
//
// See also: `harness-template-recursion-dos-buggy/` to confirm your
// harness shape actually exercises the bug.

const MAX_DEPTH: number = 8;

// === BEGIN: replace this with the real function under audit ===
function process_under_audit(input: string): number {
    __CPROVER_assert_input_size_bounded(input, MAX_DEPTH);
    return input.length;
}
// === END ===

function harness(): void {
    // Production-code precondition: the application caller enforces
    // input.length <= MAX_DEPTH before calling. We model that as
    // an assume so the verification only explores conforming
    // inputs.
    const input: string = nondet_string();
    __CPROVER_assume(input.length <= MAX_DEPTH);
    process_under_audit(input);
}
harness();
