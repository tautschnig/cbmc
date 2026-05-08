// KNOWNBUG: Array.prototype.splice with symbolic start/deleteCount.
// ES2024 §23.1.3.30. Splice mutates the array. For symbolic args,
// the length change and element shift is hard — would need a
// symbolic memmove analog. Currently returns the array unchanged
// (or only decrements length) when args are symbolic.
const a: number[] = [1, 2, 3, 4, 5];
const k: number = nondet_number();
__CPROVER_assume(k >= 1 && k <= 3);
a.splice(0, k);
// After splice(0, k): length is 5-k, a[0] is the element originally at index k
console.assert(a.length === 5 - k);
console.assert(a[0] === k + 1);
