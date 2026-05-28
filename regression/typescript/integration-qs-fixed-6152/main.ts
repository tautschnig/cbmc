// Integration test: qs@6.15.2 (fixed version of GHSA-q8mj-m7cp-5q26).
//
// The fix adds a null-guard before dereferencing. Defensive code
// that calls __CPROVER_assert_not_null AFTER the guard documents
// the contract and verifies it holds.
//
// Source: qs@6.15.2 lib/utils.js (encode with null-guard).

function encode_fixed(str: number | null): number {
    // Defensive guard before the dereference.
    if (str === null || str === undefined) {
        return -1;  // safe early-return
    }
    // After the guard, str is guaranteed non-null.
    __CPROVER_assert_not_null(str);
    return str;  // simulate `str.length`
}

function harness(): void {
    // Test with multiple inputs.
    encode_fixed(42);
    encode_fixed(null);
    encode_fixed(undefined);
}
harness();
