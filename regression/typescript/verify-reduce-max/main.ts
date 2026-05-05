const arr: number[] = [3, 7, 2, 9, 4];
const max: number = arr.reduce(
  (a: number, b: number): number => a > b ? a : b, arr[0]);
console.assert(max === 9);
