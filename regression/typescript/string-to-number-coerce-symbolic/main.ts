// ES2024 §13.5.4 + §21.1.1.1: Unary-plus on a symbolic string now
// routes through the refined string solver via
// cprover_string_parse_int_func.
const s: string = nondet_string();
__CPROVER_assume(s === "42");
const v: number = +s;
console.assert(v === 42);

// Different value
const t: string = nondet_string();
__CPROVER_assume(t === "100");
const w: number = +t;
console.assert(w === 100);

// Negative
const u: string = nondet_string();
__CPROVER_assume(u === "-7");
const z: number = +u;
console.assert(z === -7);
