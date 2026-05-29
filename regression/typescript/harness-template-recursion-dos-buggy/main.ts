// =====================================================================
// Harness Template: Recursion-DoS (BUGGY form)
// =====================================================================
//
// **When to use**:
//   You suspect a function lacks an input-size precondition, and
//   want to prove it with a sanity-check harness. The assertion
//   should FAIL, demonstrating the missing guard.
//
// **What this verifies**:
//   For an unbounded nondet input, the precondition is reachable
//   with a length > MAX_DEPTH. CBMC's symbolic search produces such
//   a witness and reports VERIFICATION FAILED.
//
// See also: `harness-template-recursion-dos-defensive/` for the
// fixed-form template you should converge on.

const MAX_DEPTH: number = 8;

// === BEGIN: function being audited ===
//
// Real code: a function that recurses for each character of `input`.
// E.g., a stack-based parser, a tree-walking decoder, an
// XML/JSON deserializer.
//
function process_under_audit(input: string): number {
    // Verification probe: if the harness's nondet source can produce
    // an input longer than MAX_DEPTH, this assertion fails — proving
    // the caller-side bug.
    __CPROVER_assert_input_size_bounded(input, MAX_DEPTH);
    return input.length;
}
// === END ===

function harness(): void {
    // Unbounded nondet string source: the model's string length is
    // nondet up to the declared inline-array bound (typically 64).
    // CBMC will pick a witness with length > MAX_DEPTH and fail the
    // precondition.
    const input: string = nondet_string();
    process_under_audit(input);
}
harness();
