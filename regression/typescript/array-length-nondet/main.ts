const arr: number[] = [1, 2, 3, 4, 5];
const n: number = nondet_number();
__CPROVER_assume(n >= 0);
__CPROVER_assume(n < arr.length);
console.assert(arr[n] >= 1);
