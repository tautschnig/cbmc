// KNOWNBUG: String.prototype.padStart with symbolic target length.
// ES2024 §22.1.3.17. Spec: returns a string of length max(source.length, n)
// padded with the pad char.
//
// Similar to repeat: variable-length result needs per-slot if_exprt
// or refined string solver integration.
const n: number = nondet_number();
__CPROVER_assume(n >= 5 && n <= 10);
const s: string = "x".padStart(n, "0");
console.assert(s.length === n);
