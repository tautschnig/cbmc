declare function nondet_array(): number[];
const arr: number[] = nondet_array();
__CPROVER_assume(arr.length === 3);
__CPROVER_assume(arr[0] > 0);
__CPROVER_assume(arr[1] > 0);
__CPROVER_assume(arr[2] > 0);
console.assert(arr[0] + arr[1] + arr[2] > 0);
