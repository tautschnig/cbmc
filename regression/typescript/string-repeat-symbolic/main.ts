// KNOWNBUG: String.prototype.repeat with symbolic count produces a
// nondet result, so the length invariant fails to verify.
//
// ES2024 §22.1.3.14. The spec says repeat(n) produces a string of
// length source.length * n. For symbolic n, we'd need a per-slot
// if_exprt over all possible n values (our fixed-size string model
// can express this up to MAX). Not yet implemented.
//
// A precise encoding would emit: result.length = src.length * n,
// result.data[i] = (i < result.length) ? src.data[i % src.length] : 0.
//
// The refined string solver (src/solvers/strings/) handles this
// natively but is not yet integrated (see
// doc/over-approximation-audit.md).
const n: number = nondet_number();
__CPROVER_assume(n >= 0 && n <= 5);
const r: string = "ab".repeat(n);
console.assert(r.length === 2 * n);
