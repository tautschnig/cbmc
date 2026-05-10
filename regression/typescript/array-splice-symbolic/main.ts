// ES2024 §23.1.3.30: Array.prototype.splice with symbolic deleteCount.
// For a constant source array and constant start with symbolic
// deleteCount, we now shift elements via a per-slot if_exprt chain.

// Splice(0, k) with k in [1,3]: shift left by k, length decreases by k.
// Constrain k to be an integer (splice internally calls
// ToIntegerOrInfinity, so fractional values would be floored;
// our test asserts the length relation directly).
const a: number[] = [1, 2, 3, 4, 5];
const k: number = nondet_number();
__CPROVER_assume(k === 1 || k === 2 || k === 3);
a.splice(0, k);
console.assert(a.length === 5 - k);
// a[0] after splice(0, k) is the element originally at index k
console.assert(a[0] === k + 1);

// Splice(1, m): preserve a[0], shift the rest
const b: number[] = [10, 20, 30, 40];
const m: number = nondet_number();
__CPROVER_assume(m === 1 || m === 2);
b.splice(1, m);
console.assert(b.length === 4 - m);
console.assert(b[0] === 10);
console.assert(b[1] === 20 + m * 10);
