function binarySearch(arr: number[], target: number): number {
  let lo: number = 0;
  let hi: number = arr.length - 1;
  while (lo <= hi) {
    const mid: number = (lo + hi) / 2;
    if (arr[mid] === target) return mid;
    if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}
const sorted: number[] = [1, 3, 5, 7, 9, 11, 13];
console.assert(binarySearch(sorted, 7) === 3);
console.assert(binarySearch(sorted, 1) === 0);
console.assert(binarySearch(sorted, 4) === -1);
