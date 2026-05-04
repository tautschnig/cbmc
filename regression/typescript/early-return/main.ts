function clamp(x: number, lo: number, hi: number): number {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}
console.assert(clamp(5, 0, 10) === 5);
console.assert(clamp(-1, 0, 10) === 0);
console.assert(clamp(15, 0, 10) === 10);
