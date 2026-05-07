function binSearch(arr: number[], target: number): number {
  let lo: number = 0;
  let hi: number = 7; // arr.length - 1
  while (lo <= hi) {
    const mid: number = (lo + hi) >> 1;
    if (arr[mid] === target) return mid;
    if (arr[mid] < target) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}
const sorted: number[] = [1, 3, 5, 7, 9, 11, 13, 15];
console.assert(binSearch(sorted, 7) === 3);
console.assert(binSearch(sorted, 15) === 7);
console.assert(binSearch(sorted, 1) === 0);
