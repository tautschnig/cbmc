// Regression for: ES2024 §13.5.4 UnaryPlus → ToNumber on strings.
// Verified working 2026-05-14 (matrix had previously marked this ❌
// but the conversion-time parse path was implemented). Test pins
// the supported cases:
//   - constant numeric strings parse to their value
//   - whitespace is trimmed
//   - empty string → 0
//   - non-numeric → NaN
function main(): void {
    console.assert(+"42" === 42);
    console.assert(+"3.14" === 3.14);
    console.assert(+"-5" === -5);
    console.assert(+"" === 0);
    console.assert(+"  10  " === 10);
    console.assert(Number.isNaN(+"abc"));
}
main();
