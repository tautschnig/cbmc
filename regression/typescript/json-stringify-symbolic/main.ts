// KNOWNBUG: JSON.stringify with non-constant input falls back to
// nondet. Symbolic values would require the refined string solver
// for variable-length string construction.
// ES2024 §25.5.2.
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 5);
const s: string = JSON.stringify(x);
// Expected: length >= 1 (at least one digit)
console.assert(s.length >= 1);
