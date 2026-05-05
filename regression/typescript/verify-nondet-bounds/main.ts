const arr: number[] = [10, 20, 30, 40, 50];
const i: number = nondet_number();
__CPROVER_assume(i >= 0 && i < 5);
console.assert(arr[i] >= 10);
console.assert(arr[i] <= 50);
