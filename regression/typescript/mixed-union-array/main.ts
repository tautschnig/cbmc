// (number | number[])[] previously crashed simplify_index's
// postcondition. After the core defensive guard in
// src/util/simplify_expr_array.cpp and the operand-type-based
// frontend bailout, mixed-element-type array literals no longer
// crash; they verify with a nondet over-approximation.
//
// See typescript-known-limitations §2.8.

function main(): void {
    const x: (number | number[])[] = [1, [2, 3], 4];
    // The first element is constrained to be one of {nondet} after
    // the conservative fallback. The assertion below is therefore
    // not guaranteed to succeed in general, but the program MUST
    // NOT crash during conversion or simplification.
    const first = x[0];
    if (typeof first === "number") {
        // best-effort: with nondet, this is unconstrained but not
        // contradictory.
        const _y = first + 1;
        console.assert(true);
    } else {
        console.assert(true);
    }
}
main();
