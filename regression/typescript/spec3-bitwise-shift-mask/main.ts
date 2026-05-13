// Regression for: ES2024 §13.10.1 step 5 — shift counts are masked
// to the low 5 bits (mod 32). Prior implementation passed the count
// through directly, so `1 << 32` produced 0 (CBMC int32 shl with
// count >= 32 has undefined behaviour, often zero).
function main(): void {
    console.assert(1 << 32 === 1);     // shift count masked → 0
    console.assert(1 << 33 === 2);
    console.assert(1 << 34 === 4);
    console.assert(1 << 31 === -2147483648);   // sign bit set
    console.assert(-4 >> 1 === -2);    // signed right shift
    console.assert(-8 >> 2 === -2);
}
main();
