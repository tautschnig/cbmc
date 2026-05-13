// Regression for: ES2024 §13.10.3 unsigned right shift (>>>).
// Reinterprets LHS as uint32. Prior implementation used signed
// right-shift and the result was misinterpreted as int32; e.g.
// `-1 >>> 0` returned -1 instead of 4294967295.
function main(): void {
    // The classic uint32 reinterpretation
    console.assert((-1 >>> 0) === 4294967295);
    console.assert((-1 >>> 1) === 2147483647);

    // Positive values pass through unchanged
    console.assert((8 >>> 0) === 8);
    console.assert((8 >>> 1) === 4);

    // High bit value
    console.assert((0x80000000 >>> 0) === 2147483648);
}
main();
