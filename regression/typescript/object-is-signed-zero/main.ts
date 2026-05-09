// KNOWNBUG: Object.is(0, -0) should return false per ES2024 §20.1.2.11
// (SameValue treats +0 and -0 as distinct). Our implementation uses
// IEEE float equality for the non-NaN case, which considers +0 === -0
// to be true. Distinguishing signed zero would require bit-pattern
// comparison.
console.assert(!Object.is(0, -0));
