// Regression for: ternary with literal-union type inference.
// TS infers `10 | 20` for `0 ? 10 : 20`. Previously this created a
// tagged-union struct that couldn't be compared to a plain number.
// Now numeric literal unions collapse to double_type().
function main(): void {
    const a = 0 ? 10 : 20;
    console.assert(a === 20);
    const b = 1 ? 10 : 20;
    console.assert(b === 10);
    // Nested
    const n = 5;
    const c = n > 0 ? 1 : n < 0 ? -1 : 0;
    console.assert(c === 1);
}
main();
