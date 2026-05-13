// Regression for: ES2024 §13.13 && and || return one of the operand
// values (not a boolean): `1 && 42 === 42`, `0 || 42 === 42`. Prior
// implementation always returned a boolean, so assigning `1 && 42`
// to a number variable produced 1 (the bool-to-number coercion of
// true), not 42.
function main(): void {
    // && short-circuits on falsy left (returns left).
    const a = 0 && 5;
    console.assert(a === 0);

    // && returns right when left is truthy.
    const b = 1 && 42;
    console.assert(b === 42);

    // || returns left when left is truthy.
    const c = 7 || 99;
    console.assert(c === 7);

    // || returns right when left is falsy.
    const d = 0 || 42;
    console.assert(d === 42);
}
main();
