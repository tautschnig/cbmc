declare function nondet_array(): number[];
const arr: number[] = nondet_array();
__CPROVER_assume(arr.length === 5);
__CPROVER_assume(arr[0] === 1);
__CPROVER_assume(arr[4] === 5);
console.assert(arr[0] + arr[4] === 6);
