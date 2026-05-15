// ES2024 §6.1.6.2.9: BigInt bitwise operators.
function main(): void {
    console.assert((0xFFn & 0x0Fn) === 0x0Fn);
    console.assert((0xF0n | 0x0Fn) === 0xFFn);
    console.assert((0xFFn ^ 0x0Fn) === 0xF0n);
    console.assert((1n << 10n) === 1024n);
    console.assert((1024n >> 5n) === 32n);
    console.assert((1n << 64n) === 18446744073709551616n);
}
main();
