// Regression: optional-type fields (x?: T) must initialize from the
// object literal, not default to undefined. Previously the frontend
// kept the trailing '?' in the component name, so literal values
// assigned by name didn't match the struct's component.
const a: { x?: number } = { x: 3.14 };
console.assert(a.x === 3.14);

// With ?. and ?? together
const b: { y?: number } = { y: 42 };
const bv: number = b?.y ?? 0;
console.assert(bv === 42);

// Optional string field
const c: { s?: string } = { s: "hello" };
console.assert(c.s === "hello");

// Multiple optional fields
const d: { m?: number; n?: number } = { m: 1, n: 2 };
console.assert(d.m === 1);
console.assert(d.n === 2);
