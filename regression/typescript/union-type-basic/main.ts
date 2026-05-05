// Union type with typeof narrowing
function getLength(x: number | string): number {
  if (typeof x === "string") {
    return x.length;
  }
  return x;
}
console.assert(getLength(5) === 5);
console.assert(getLength("hi") === 2);
