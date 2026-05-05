declare function nondet_array(): number[];
const arr: number[] = nondet_array();
__CPROVER_assume(arr.length > 0);
__CPROVER_assume(arr.length < 5);
console.assert(arr.length > 0);
console.assert(arr.length < 5);
