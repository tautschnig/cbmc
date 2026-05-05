function countBits(n: number): number {
  let count: number = 0;
  let x: number = n;
  while (x > 0) {
    count += x & 1;
    x = x >> 1;
  }
  return count;
}
console.assert(countBits(7) === 3);
console.assert(countBits(8) === 1);
console.assert(countBits(15) === 4);
