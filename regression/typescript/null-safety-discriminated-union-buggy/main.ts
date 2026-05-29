// Companion to `null-safety-discriminated-union/`: SAME recipe, but
// the consumer skips the `_present` guard. Demonstrates that
// __CPROVER_assert_not_null catches the missing guard at the
// dereference site.
//
// See typescript-known-limitations.md §1.7 for the recipe and rationale.

type NullableString = { _present: boolean; value: string };

function nondet_nullable_string(): NullableString {
    return {
        _present: nondet_boolean(),
        value: nondet_string(),
    };
}

/** Buggy consumer — reads `value` without checking `_present`. */
function buggy_consumer(input: NullableString): number {
    // Bug: missing `if (!input._present) return -1;` guard.
    // The recipe contract says: callers/harnesses can pass a wrapper with
    // _present === false; defensive code MUST check _present before
    // reading `value`. We assert that contract here at the dereference
    // site; the harness exercises the missing-guard case and the
    // assertion is expected to fail.
    __CPROVER_assert(input._present, "value is only defined when _present is true");
    return input.value.length;
}

function harness(): void {
    const x = nondet_nullable_string();
    buggy_consumer(x);
}

harness();
