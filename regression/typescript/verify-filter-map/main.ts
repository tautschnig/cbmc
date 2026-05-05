const nums: number[] = [1, 2, 3, 4, 5, 6, 7, 8];
const evenDoubled: number[] = nums
  .filter((x: number): boolean => x % 2 === 0)
  .map((x: number): number => x * 2);
console.assert(evenDoubled[0] === 4);
console.assert(evenDoubled[1] === 8);
