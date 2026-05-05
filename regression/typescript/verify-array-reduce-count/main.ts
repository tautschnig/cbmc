const arr: number[] = [1, 2, 3, 4, 5, 6, 7, 8];
const countEven: number = arr.reduce(
  (acc: number, x: number): number => x % 2 === 0 ? acc + 1 : acc, 0);
console.assert(countEven === 4);
