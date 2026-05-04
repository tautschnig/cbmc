function sign(x: number): number {
  if (x > 0) return 1;
  if (x < 0) return -1;
  return 0;
}
console.assert(sign(5) === 1);
console.assert(sign(-3) === -1);
console.assert(sign(0) === 0);
