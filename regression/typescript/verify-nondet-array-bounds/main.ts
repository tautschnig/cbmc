const arr: number[] = [1, 2, 3, 4, 5];
const i: number = nondet_number();
__CPROVER_assume(i >= 0);
__CPROVER_assume(i < arr.length);
const val: number = arr[i];
console.assert(val >= 1);
console.assert(val <= 5);
