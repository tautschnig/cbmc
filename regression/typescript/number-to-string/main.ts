// ES2024 §21.1.3.6: Number.prototype.toString(radix).
console.assert((42).toString() === "42");
console.assert((0).toString() === "0");
console.assert((-42).toString() === "-42");

// Radix conversions
console.assert((10).toString(2) === "1010");
console.assert((10).toString(8) === "12");
console.assert((10).toString(16) === "a");
console.assert((255).toString(16) === "ff");
console.assert((0).toString(16) === "0");
console.assert((-10).toString(2) === "-1010");
