function safeGet(arr: number[], idx: number, def: number): number {
  if (idx < 0) return def;
  if (idx >= arr.length) return def;
  return arr[idx];
}
const data: number[] = [10, 20, 30];
console.assert(safeGet(data, 1, 0) === 20);
console.assert(safeGet(data, 5, -1) === -1);
