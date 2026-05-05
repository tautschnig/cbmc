function min(arr: number[]): number {
  let m: number = arr[0];
  for (let i = 1; i < arr.length; i++) {
    if (arr[i] < m) m = arr[i];
  }
  return m;
}
const data: number[] = [5, 3, 8, 1, 9, 2];
console.assert(min(data) === 1);
