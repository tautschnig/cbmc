// ES2024 §22.1.3.30: toUpperCase on a symbolic string preserves
// character content precisely through the refined-string solver.
const a: string = nondet_string();
__CPROVER_assume(a === "hello");
const u: string = a.toUpperCase();
console.assert(u.length === 5);
console.assert(u === "HELLO");
