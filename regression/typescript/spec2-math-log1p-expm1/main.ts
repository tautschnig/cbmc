// Regression for: ES2024 §21.3.2 Math.log1p and Math.expm1.
// Prior implementation didn't include these — they fell through to
// nondet.
function main(): void {
    console.assert(Math.log1p(0) === 0);
    console.assert(Math.expm1(0) === 0);
    // log1p(x) = ln(1+x) — small values
    const x1 = Math.log1p(1);
    // ln(2) ≈ 0.693147
    console.assert(x1 > 0.69 && x1 < 0.70);

    // expm1(1) ≈ e - 1 ≈ 1.71828
    const e1 = Math.expm1(1);
    console.assert(e1 > 1.71 && e1 < 1.72);
}
main();
