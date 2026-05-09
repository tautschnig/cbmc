// ES2024 §22.1.3.14: String.prototype.repeat with symbolic count.
// For a constant source string, we now emit length = src.length * n
// directly, so length assertions verify even though the content is
// nondet (content would need the refined string solver).
const n: number = nondet_number();
__CPROVER_assume(n >= 0 && n <= 5);
const r: string = "ab".repeat(n);
console.assert(r.length === 2 * n);

// Different source length
const m: number = nondet_number();
__CPROVER_assume(m >= 0 && m <= 4);
const r2: string = "xyz".repeat(m);
console.assert(r2.length === 3 * m);

// Zero count
const r0: string = "ab".repeat(0);
console.assert(r0.length === 0);
