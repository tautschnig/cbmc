interface Comparable {
  value: number;
}
function compare(a: Comparable, b: Comparable): number {
  if (a.value < b.value) return -1;
  if (a.value > b.value) return 1;
  return 0;
}
const x: Comparable = { value: 5 };
const y: Comparable = { value: 3 };
console.assert(compare(x, y) === 1);
console.assert(compare(y, x) === -1);
console.assert(compare(x, x) === 0);
