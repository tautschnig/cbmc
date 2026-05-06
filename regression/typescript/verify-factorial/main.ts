function fact(n: number): number {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
console.assert(fact(1) === 1);
console.assert(fact(4) === 24);
console.assert(fact(6) === 720);
