function findMax(arr: number[]): number {
  let max: number = arr[0];
  for (let i = 1; i < arr.length; i++) {
    if (arr[i] > max) max = arr[i];
  }
  return max;
}
const nums: number[] = [5, 3, 8, 1, 9, 2];
console.assert(findMax(nums) === 9);
