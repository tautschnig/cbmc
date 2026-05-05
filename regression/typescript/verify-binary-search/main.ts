function binarySearch(arr: number[], target: number, lo: number, hi: number): number {
  if (lo > hi) return -1;
  const mid: number = lo + Math.floor((hi - lo) / 2);
  if (arr[mid] === target) return mid;
  if (arr[mid] < target) return binarySearch(arr, target, mid + 1, hi);
  return binarySearch(arr, target, lo, mid - 1);
}
const sorted: number[] = [1, 3, 5, 7];
console.assert(binarySearch(sorted, 5, 0, 3) === 2);
console.assert(binarySearch(sorted, 4, 0, 3) === -1);
