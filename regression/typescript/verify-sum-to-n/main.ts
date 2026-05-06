function sumTo(n: number): number {
  let s: number = 0;
  for (let i = 1; i <= n; i++) s = s + i;
  return s;
}
console.assert(sumTo(5) === 15);
console.assert(sumTo(10) === 55);
