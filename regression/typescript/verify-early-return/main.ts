function findFirst(arr: number[], target: number): number {
  for (let i = 0; i < arr.length; i++) {
    if (arr[i] === target) return i;
  }
  return -1;
}
const data: number[] = [5, 3, 8, 1, 9];
console.assert(findFirst(data, 8) === 2);
console.assert(findFirst(data, 99) === -1);
