const r: number = Math.random();
__CPROVER_assume(r >= 0);
console.assert(r >= 0);
