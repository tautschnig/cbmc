// ES2024 §21.1.2.3/5: Number.isInteger and isSafeInteger handle negatives.
console.assert(Number.isInteger(-5));
console.assert(Number.isInteger(0));
console.assert(Number.isInteger(-0));
console.assert(!Number.isInteger(3.14));
console.assert(!Number.isInteger(-3.14));
console.assert(!Number.isInteger(NaN));
console.assert(!Number.isInteger(Infinity));
console.assert(!Number.isInteger(-Infinity));

console.assert(Number.isSafeInteger(0));
console.assert(Number.isSafeInteger(5));
console.assert(Number.isSafeInteger(-5));
console.assert(Number.isSafeInteger(Number.MAX_SAFE_INTEGER));
console.assert(Number.isSafeInteger(Number.MIN_SAFE_INTEGER));
console.assert(!Number.isSafeInteger(3.14));
console.assert(!Number.isSafeInteger(NaN));
console.assert(!Number.isSafeInteger(Infinity));
