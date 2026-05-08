// KNOWNBUG: String.prototype.indexOf with symbolic needle.
// ES2024 §22.1.3.9. We can't symbolically search for an arbitrary-
// length pattern in a string; requires the refined string solver.
// Currently returns nondet for a symbolic needle.
const s: string = "hello world";
const needle: string = nondet_string();
__CPROVER_assume(needle.length === 3);
__CPROVER_assume(needle === "llo");
const pos: number = s.indexOf(needle);
console.assert(pos === 2);
