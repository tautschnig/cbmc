// Regression for: ES2024 §21.1.1.1 Number coercion of empty string.
// Number("") === 0 per spec (unlike parseInt/parseFloat, which
// return NaN for empty). Whitespace-only strings also coerce to 0.
function main(): void {
    console.assert(Number("") === 0);
    console.assert(Number("   ") === 0);
    console.assert(Number("42") === 42);
    console.assert(Number("-1") === -1);
    console.assert(Number("3.14") === 3.14);
}
main();
