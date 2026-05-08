// ES2024 §21.1.3.3: Number.prototype.toFixed(fractionDigits).
console.assert((3.14159).toFixed(2) === "3.14");
console.assert((3.14159).toFixed(0) === "3");
console.assert((3.14159).toFixed(4) === "3.1416");
console.assert((1.5).toFixed(0) === "2");
console.assert((0).toFixed(3) === "0.000");
console.assert((100).toFixed(2) === "100.00");
const x: number = 99.99;
console.assert(x.toFixed(1) === "100.0");
// Negative literals work inline:
console.assert((-3.14).toFixed(1) === "-3.1");
// Note: negative values stored in symbols aren't resolved (known gap,
// see typescript-capability-matrix.md).
