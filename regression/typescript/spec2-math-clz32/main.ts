// Regression for: ES2024 §21.3.2.10 Math.clz32.
// Counts leading zeros in the ToUint32 representation. 0 → 32.
function main(): void {
    console.assert(Math.clz32(1) === 31);
    console.assert(Math.clz32(0) === 32);
    console.assert(Math.clz32(0x80000000) === 0);
    console.assert(Math.clz32(0xffffffff) === 0);
    console.assert(Math.clz32(0x40000000) === 1);
    console.assert(Math.clz32(0x00010000) === 15);
}
main();
