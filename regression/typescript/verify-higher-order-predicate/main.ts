function countWhere(arr: number[], pred: (x: number) => boolean): number {
  let count: number = 0;
  for (let i = 0; i < arr.length; i++) {
    if (pred(arr[i])) count++;
  }
  return count;
}
const isPositive = (x: number): boolean => x > 0;
const data: number[] = [-1, 2, -3, 4, -5, 6];
console.assert(countWhere(data, isPositive) === 3);
