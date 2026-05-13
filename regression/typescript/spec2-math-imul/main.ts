// Regression for: ES2024 §21.3.2.19 Math.imul.
// Multiplies two args as int32, with 32-bit wrap-around. Prior
// implementation was missing entirely; the path also required using
// mp_integer directly (not the ansi-c-string round-trip, which
// silently loses precision on values > 1e7).
function main(): void {
    console.assert(Math.imul(2, 3) === 6);
    console.assert(Math.imul(-1, 8) === -8);
    console.assert(Math.imul(0xffffffff, 5) === -5);   // wrap
    console.assert(Math.imul(0x7fffffff, 2) === -2);   // overflow wrap
    console.assert(Math.imul(0, 100) === 0);
}
main();
