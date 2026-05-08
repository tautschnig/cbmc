// KNOWNBUG: Array.prototype.sort on an array with SYMBOLIC elements.
// ES2024 §23.1.3.29. Even with a recognized comparator (a - b),
// we only sort at conversion time if all elements are constants.
// Symbolic elements produce an unchanged array.
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 10);
const y: number = nondet_number();
__CPROVER_assume(y >= 1 && y <= 10);
const arr: number[] = [x, y, 5];
arr.sort((a: number, b: number) => a - b);
// After sort: arr[0] <= arr[1] <= arr[2]
console.assert(arr[0] <= arr[1]);
console.assert(arr[1] <= arr[2]);
