// ES2024 §22.1.3.7: String.prototype.includes on a symbolic
// receiver, constrained by assumption, routes through the refined
// string solver (cprover_string_contains_func).
const s: string = nondet_string();
__CPROVER_assume(s === "hello world");
console.assert(s.includes("world"));
console.assert(s.includes("hello"));
console.assert(!s.includes("xyz"));

// Symbolic needle in constrained string
const needle: string = nondet_string();
__CPROVER_assume(needle === "world");
const s2: string = "hello world";
console.assert(s2.includes(needle));
