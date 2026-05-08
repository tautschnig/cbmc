// KNOWNBUG: +"42" unary plus on a string should parse to number.
// ES2024 §13.5.4 UnaryPlus + §21.1.1.1 ToNumber(string).
// Not implemented — the unary-plus handler returns nondet for
// string operands. A precise implementation would need symbolic
// digit parsing, i.e., the refined string solver.
const v: number = +"42";
console.assert(v === 42);
