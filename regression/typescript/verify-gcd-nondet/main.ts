function gcd(a: number, b: number): number {
  while (b !== 0) { const t = b; b = a % b; a = t; }
  return a;
}
const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x > 0 && x < 20);
__CPROVER_assume(y > 0 && y < 20);
const g: number = gcd(x, y);
console.assert(g > 0);
