function count(arr: number[], target: number, idx: number): number {
  if (idx >= arr.length) return 0;
  return (arr[idx] === target ? 1 : 0) + count(arr, target, idx + 1);
}
const a: number[] = [1, 2, 3, 2, 1, 2];
console.assert(count(a, 2, 0) === 3);
