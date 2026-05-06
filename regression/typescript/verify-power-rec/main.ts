function power(base: number, exp: number): number {
  if (exp === 0) return 1;
  return base * power(base, exp - 1);
}
console.assert(power(2, 10) === 1024);
console.assert(power(3, 4) === 81);
