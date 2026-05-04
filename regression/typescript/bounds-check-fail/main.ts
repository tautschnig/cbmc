// Verification should detect potential assertion violation
const x: number = nondet_number();
__CPROVER_assume(x >= 0);
__CPROVER_assume(x <= 10);
console.assert(x < 5); // should fail — x can be 5..10
