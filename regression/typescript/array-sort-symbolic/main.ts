// ES2024 §23.1.3.29: Array.prototype.sort with symbolic elements,
// using a bubble-sort compare-and-swap network emitted at conversion
// time. Each slot becomes an if_exprt chain over pairwise comparisons.

// Ascending: (a, b) => a - b
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 10);
const y: number = nondet_number();
__CPROVER_assume(y >= 1 && y <= 10);
const arr: number[] = [x, y, 5];
arr.sort((a: number, b: number) => a - b);
console.assert(arr[0] <= arr[1]);
console.assert(arr[1] <= arr[2]);

// Descending: (a, b) => b - a
const p: number = nondet_number();
__CPROVER_assume(p >= 1 && p <= 10);
const q: number = nondet_number();
__CPROVER_assume(q >= 1 && q <= 10);
const arr2: number[] = [p, q, 5];
arr2.sort((a: number, b: number) => b - a);
console.assert(arr2[0] >= arr2[1]);
console.assert(arr2[1] >= arr2[2]);
