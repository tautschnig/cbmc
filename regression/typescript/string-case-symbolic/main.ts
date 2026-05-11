// ES2024 §22.1.3.25-26: toUpperCase/toLowerCase on symbolic strings
// routed through the refined-string solver.
const s: string = nondet_string();
__CPROVER_assume(s === "hello");
console.assert(s.toUpperCase() === "HELLO");

const t: string = nondet_string();
__CPROVER_assume(t === "WORLD");
console.assert(t.toLowerCase() === "world");

// Length is preserved
const u: string = nondet_string();
__CPROVER_assume(u === "MixedCase");
console.assert(u.toUpperCase().length === 9);
console.assert(u.toLowerCase().length === 9);
