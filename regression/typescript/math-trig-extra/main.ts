// ES2024 §21.3.2: additional trig + log methods.
// Using epsilon because floats are tricky with transcendentals.
const t0: number = Math.tan(0);
console.assert(t0 > -0.001 && t0 < 0.001);
const a0: number = Math.asin(0);
console.assert(a0 > -0.001 && a0 < 0.001);
const ac1: number = Math.acos(1);
console.assert(ac1 > -0.001 && ac1 < 0.001);
const at0: number = Math.atan(0);
console.assert(at0 > -0.001 && at0 < 0.001);
// atan2
const at2: number = Math.atan2(1, 1); // pi/4
console.assert(at2 > 0.78 && at2 < 0.79);
// log2, log10
console.assert(Math.log2(8) === 3);
console.assert(Math.log10(1000) === 3);
