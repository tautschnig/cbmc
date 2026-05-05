// Map then reduce (constant array chain)
const data: number[] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
const squareSum: number = data
  .map((x: number): number => x * x)
  .reduce((a: number, b: number): number => a + b, 0);
console.assert(squareSum === 385);
