// Regression: template literal interpolation of non-literal numeric
// expressions (unary minus, arithmetic) previously fell back to nondet,
// leading to unconstrained length. Constant-folding now covers these
// cases.

// Simple negative: "v=-1"
const a = `v=${-1}`;
console.assert(a.length === 4);

// Multiplication with negative: "v=-42"
const b = `v=${42 * -1}`;
console.assert(b.length === 5);

// Arithmetic chain: "v=-4200"
const c = `v=${(100 * 42) * -1}`;
console.assert(c.length === 7);

// Plus: "v=3"
const d = `v=${1 + 2}`;
console.assert(d.length === 3);

// Minus: "v=3"
const e = `v=${5 - 2}`;
console.assert(e.length === 3);
