// Regression for: ES2024 §14.7 for-loop iter runs before re-checking
// cond; `continue` in the body must re-run iter. Prior implementation
// emitted the iter expression as a dead EXPRESSION (no side effect),
// so the loop variable never advanced. Additionally, the continue-
// target was positioned after iter, so even with a proper assign,
// continue would skip it.
function main(): void {
    // break
    let i = 0;
    while (i < 10) {
        if (i === 3) break;
        i++;
    }
    console.assert(i === 3);

    // continue: j=2 should be skipped but iter must still advance.
    // Sum = 0 + 1 + 3 + 4 = 8.
    let sum = 0;
    for (let j = 0; j < 5; j++) {
        if (j === 2) continue;
        sum += j;
    }
    console.assert(sum === 8);

    // Plain for-loop without continue.
    let product = 1;
    for (let k = 1; k <= 4; k++) {
        product *= k;
    }
    console.assert(product === 24);
}
main();
