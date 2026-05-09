// ES2024 §20.1.2.11 (SameValue): Object.is distinguishes +0 and -0.
// Our front-end detects this for constant zeros.
console.assert(!Object.is(0, -0));
console.assert(!Object.is(-0, 0));
console.assert(Object.is(0, 0));
console.assert(Object.is(-0, -0));
// Other cases remain correct
console.assert(Object.is(1, 1));
console.assert(!Object.is(1, 2));
console.assert(Object.is(NaN, NaN));
