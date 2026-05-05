const n: number = nondet_number();
__CPROVER_assume(n >= 0 && n <= 100);
const isEven: boolean = n % 2 === 0;
const isOdd: boolean = n % 2 !== 0;
console.assert(isEven || isOdd);
