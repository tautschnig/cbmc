function applyToAll(arr: number[], f: (x: number) => number): number[] {
  return arr.map(f);
}
const triple = (x: number): number => x * 3;
const result: number[] = applyToAll([1, 2, 3], triple);
console.assert(result[0] === 3);
console.assert(result[2] === 9);
