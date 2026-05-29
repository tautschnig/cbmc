// Recipe: discriminated-union pattern for null-safe reference inputs
// =====================================================================
//
// Documents the supported workaround for typescript-known-limitations
// §1.7: reference-typed unions like `string | null` collapse to the
// reference type silently in our model. Naive `if (x === null)` checks
// against such inputs miss real null derefs.
//
// **Recipe**: wrap reference-typed inputs in a discriminated-union
// struct `{ _present: boolean, value: T }`. The caller (or harness) sets
// `_present` non-deterministically; the receiving function MUST guard on
// `_present` before reading `value`.
//
// This file is a *positive* regression test: a function that uses the
// recipe correctly verifies. The companion test
// `null-safety-discriminated-union-buggy/` shows the same pattern with
// the missing guard, where __CPROVER_assert_not_null catches the bug.
//
// Note on generics: in the current frontend we use a concrete struct
// type per T rather than `Nullable<T>` because converter support for
// generics in object-literal returns is incomplete.

/** Wrapper for a may-be-null reference-typed string input. */
type NullableString = { _present: boolean; value: string };

/** Construct a non-deterministic NullableString for harness inputs. */
function nondet_nullable_string(): NullableString {
    return {
        _present: nondet_boolean(),
        value: nondet_string(),
    };
}

/** Defensive consumer — guards on `_present` before reading. */
function safe_consumer(input: NullableString): number {
    if (!input._present) {
        return -1;
    }
    // After the guard, `input.value` is safe to read.
    return input.value.length;
}

function harness(): void {
    const x = nondet_nullable_string();
    const result = safe_consumer(x);
    // safe_consumer returns -1 for missing input; otherwise non-negative.
    __CPROVER_assert(result >= -1, "safe_consumer returns either -1 or non-negative length");
}

harness();
