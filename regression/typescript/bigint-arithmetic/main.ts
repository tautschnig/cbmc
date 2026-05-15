// ES2024 §6.1.6.2: BigInt arithmetic.
function main(): void {
    const a: bigint = 100n;
    const b: bigint = 7n;
    console.assert(a + b === 107n);
    console.assert(a - b === 93n);
    console.assert(a * b === 700n);
    console.assert(a / b === 14n);    // truncates toward zero
    console.assert(a % b === 2n);
    console.assert(-a === -100n);
    console.assert(-10n / 3n === -3n);

    // BigInt() constructor
    const c = BigInt(42);
    console.assert(c === 42n);
    console.assert(c + 8n === 50n);
}
main();
