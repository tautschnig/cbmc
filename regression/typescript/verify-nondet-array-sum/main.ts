declare function nondet_array(): number[];
const arr: number[] = nondet_array();
__CPROVER_assume(arr.length === 3);
__CPROVER_assume(arr[0] >= 0 && arr[0] <= 10);
__CPROVER_assume(arr[1] >= 0 && arr[1] <= 10);
__CPROVER_assume(arr[2] >= 0 && arr[2] <= 10);
const sum: number = arr[0] + arr[1] + arr[2];
console.assert(sum >= 0);
console.assert(sum <= 30);
