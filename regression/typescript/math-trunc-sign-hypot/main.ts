// ES2024 §21.3.2: Math.trunc, sign, hypot, cbrt
console.assert(Math.trunc(3.7) === 3);
console.assert(Math.trunc(-3.7) === -3);
console.assert(Math.trunc(0.5) === 0);
console.assert(Math.trunc(-0.5) === 0);

console.assert(Math.sign(5) === 1);
console.assert(Math.sign(-5) === -1);
console.assert(Math.sign(0) === 0);

console.assert(Math.hypot(3, 4) === 5);
console.assert(Math.hypot(5, 12) === 13);
console.assert(Math.hypot(0, 0) === 0);

// cbrt may have tiny fp error — use epsilon
const r27: number = Math.cbrt(27);
console.assert(r27 > 2.999 && r27 < 3.001);
const rm27: number = Math.cbrt(-27);
console.assert(rm27 > -3.001 && rm27 < -2.999);
