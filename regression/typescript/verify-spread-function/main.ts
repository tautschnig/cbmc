function max3(a: number, b: number, c: number): number {
  if (a >= b && a >= c) return a;
  if (b >= c) return b;
  return c;
}
const nums: number[] = [3, 7, 5];
console.assert(max3(...nums) === 7);
