function findMin(arr: number[]): number {
  let min: number = arr[0];
  for (let i = 1; i < arr.length; i++) {
    if (arr[i] < min) min = arr[i];
  }
  return min;
}
const nums: number[] = [5, 3, 8, 1, 9, 2];
console.assert(findMin(nums) === 1);
