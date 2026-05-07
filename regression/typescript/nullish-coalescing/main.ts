// ES2024 sec-nullish-coalescing (§13.13)
// Undefined is modeled as NaN; ?? returns RHS when LHS is NaN.

// Basic case: undefined LHS
const a: number | undefined = undefined;
const b: number = a ?? 10;
console.assert(b === 10);

// Defined LHS passes through
const c: number | undefined = 5;
const d: number = c ?? 10;
console.assert(d === 5);

// Zero is a defined value, passes through
const e: number | undefined = 0;
const f: number = e ?? 10;
console.assert(f === 0);

// Chained ??
const g: number | undefined = undefined;
const h: number | undefined = undefined;
const i: number = g ?? h ?? 99;
console.assert(i === 99);

// Middle value taken
const j: number | undefined = undefined;
const k: number | undefined = 42;
const l: number = j ?? k ?? 99;
console.assert(l === 42);
