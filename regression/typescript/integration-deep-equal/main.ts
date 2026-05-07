// Shallow deep-equal for primitives
function equal(a: number, b: number): boolean {
  return a === b;
}
function arrEqual(a: number[], b: number[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}
console.assert(equal(5, 5) === true);
console.assert(equal(5, 6) === false);
const x: number[] = [1, 2, 3];
const y: number[] = [1, 2, 3];
const z: number[] = [1, 2, 4];
console.assert(arrEqual(x, y) === true);
console.assert(arrEqual(x, z) === false);
