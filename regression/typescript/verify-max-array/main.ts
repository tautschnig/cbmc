function max(arr: number[]): number {
  let m: number = arr[0];
  for (let i = 1; i < 4; i++) {
    if (arr[i] > m) m = arr[i];
  }
  return m;
}
const data: number[] = [3, 7, 2, 9];
console.assert(max(data) === 9);
