const arr: number[] = [1, 2, 3];
const i: number = nondet_number();
__CPROVER_assume(i >= 0);
__CPROVER_assume(i < 3);
const val: number = arr[i];
console.assert(val >= 1);
