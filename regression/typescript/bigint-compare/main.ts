// ES2024 §6.1.6.2: BigInt comparison.
function main(): void {
    const a: bigint = 100n;
    const b: bigint = 7n;
    console.assert(a > b);
    console.assert(b < a);
    console.assert(a >= 100n);
    console.assert(a <= 100n);
    console.assert(a === 100n);
    console.assert(a !== b);
    console.assert(!(a === b));
}
main();
