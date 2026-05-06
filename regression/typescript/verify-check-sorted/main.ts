function isSorted(arr: number[]): boolean {
  for (let i = 1; i < arr.length; i++) {
    if (arr[i] < arr[i-1]) return false;
  }
  return true;
}
console.assert(isSorted([1, 2, 3, 4, 5]) === true);
console.assert(isSorted([1, 3, 2, 4]) === false);
