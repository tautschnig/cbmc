function classify(n: number): number {
  if (n < 0) return -1;
  if (n === 0) return 0;
  if (n < 10) return 1;
  if (n < 100) return 2;
  return 3;
}
console.assert(classify(-5) === -1);
console.assert(classify(0) === 0);
console.assert(classify(5) === 1);
console.assert(classify(50) === 2);
console.assert(classify(500) === 3);
