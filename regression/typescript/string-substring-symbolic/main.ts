// ES2024 §22.1.3.21: String.prototype.substring on a symbolic
// receiver, with the per-slot encoding working correctly now that
// the nil-init bug in the end-arg path is fixed.
const s: string = nondet_string();
__CPROVER_assume(s === "hello world");
console.assert(s.substring(0, 5) === "hello");
console.assert(s.substring(6) === "world");
console.assert(s.substring(0, 11) === "hello world");

// Symbolic start index (constrained)
const n: number = nondet_number();
__CPROVER_assume(n === 6);
console.assert(s.substring(n) === "world");
