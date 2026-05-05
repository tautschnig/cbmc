interface Comparable {
  value: number;
}
function isGreater(a: Comparable, b: Comparable): boolean {
  return a.value > b.value;
}
const x: Comparable = { value: 10 };
const y: Comparable = { value: 5 };
console.assert(isGreater(x, y) === true);
console.assert(isGreater(y, x) === false);
