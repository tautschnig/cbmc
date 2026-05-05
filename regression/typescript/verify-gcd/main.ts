function gcd(a: number, b: number): number {
  while (b !== 0) {
    const t: number = b;
    b = a % b;
    a = t;
  }
  return a;
}
console.assert(gcd(12, 8) === 4);
console.assert(gcd(15, 5) === 5);
