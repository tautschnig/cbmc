let x: number = 5;
console.assert(x === 5);
__CPROVER_havoc_object(x);
__CPROVER_assume(x > 0);
__CPROVER_assume(x < 10);
console.assert(x > 0);
console.assert(x < 10);
