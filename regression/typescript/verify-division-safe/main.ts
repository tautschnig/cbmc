function safeDivide(a: number, b: number): number {
  __CPROVER_assert(b !== 0);
  return a / b;
}
console.assert(safeDivide(10, 2) === 5);
console.assert(safeDivide(9, 3) === 3);
