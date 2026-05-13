// Regression for: ES2024 §21.3.2.32 Math.sqrt of negative input
// returns NaN. Prior implementation gated the constant-fold path on
// `arg_vals[0] >= 0`, so negative input fell through to nondet.
function main(): void {
    console.assert(Number.isNaN(Math.sqrt(-1)));
    console.assert(Number.isNaN(Math.sqrt(-4)));
    console.assert(Math.sqrt(0) === 0);
    console.assert(Math.sqrt(4) === 2);
    console.assert(Math.sqrt(9) === 3);
}
main();
