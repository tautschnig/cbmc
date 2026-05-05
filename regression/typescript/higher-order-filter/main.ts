function filterPositive(arr: number[]): number[] {
  return arr.filter((x: number): boolean => x > 0);
}
const result: number[] = filterPositive([-1, 2, -3, 4, -5]);
console.assert(result.length === 2);
console.assert(result[0] === 2);
