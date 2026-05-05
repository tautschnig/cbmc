function sign(x: number): number {
  if (x > 0) return 1;
  if (x < 0) return -1;
  return 0;
}
const a: number = nondet_number();
__CPROVER_assume(a > 0);
console.assert(sign(a) === 1);
const b: number = nondet_number();
__CPROVER_assume(b < 0);
console.assert(sign(b) === -1);
console.assert(sign(0) === 0);
