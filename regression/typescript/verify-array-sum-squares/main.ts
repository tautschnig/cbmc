const arr: number[] = [1, 2, 3, 4, 5];
const sumSq: number = arr.map((x: number): number => x * x)
  .reduce((a: number, b: number): number => a + b, 0);
console.assert(sumSq === 55);
