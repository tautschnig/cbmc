const idx: number = nondet_number();
__CPROVER_assume(idx >= 0);
__CPROVER_assume(idx < 3);
const arr: number[] = [10, 20, 30];
console.assert(arr[idx] >= 10);
console.assert(arr[idx] <= 30);
