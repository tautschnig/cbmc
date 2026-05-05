function sumTo(n: number): number {
  if (n <= 0) return 0;
  return n + sumTo(n - 1);
}
console.assert(sumTo(5) === 15);
console.assert(sumTo(10) === 55);
