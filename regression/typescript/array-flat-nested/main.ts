// KNOWNBUG: Array.prototype.flat() on nested arrays (number[][])
// crashes. Only the 1-D no-op case works.
const a: number[][] = [[1, 2], [3, 4]];
const b: number[] = a.flat();
console.assert(b.length === 4);
