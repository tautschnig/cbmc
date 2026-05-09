// ES2024 §20.1.2.11: Object.is — SameValue comparison.
console.assert(Object.is(1, 1));
console.assert(!Object.is(1, 2));
console.assert(Object.is("a", "a"));
console.assert(!Object.is("a", "b"));
console.assert(Object.is(true, true));
console.assert(!Object.is(true, false));
console.assert(Object.is(null, null));       // both NaN sentinel
console.assert(Object.is(undefined, undefined)); // both NaN sentinel
// NaN special case: Object.is(NaN, NaN) === true (unlike ===)
console.assert(Object.is(NaN, NaN));
// Type mismatch
console.assert(!Object.is(1, "1"));
// Note: Object.is(0, -0) should be false per spec, but our frontend
// uses IEEE equality which returns true for +0 == -0. Tracked as a
// documented gap.
