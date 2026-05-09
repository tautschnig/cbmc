// KNOWNBUG: +str where str is symbolic needs refined string solver.
// ES2024 §13.5.4 + §21.1.1.1.
const s: string = nondet_string();
__CPROVER_assume(s === "42");
const v: number = +s;
console.assert(v === 42);
