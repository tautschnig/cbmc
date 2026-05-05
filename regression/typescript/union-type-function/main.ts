// Union type with typeof narrowing returning number
function toNumber(x: number | string): number {
  if (typeof x === "number") {
    return x * 2;
  }
  return x.length;
}
console.assert(toNumber(5) === 10);
console.assert(toNumber("hello") === 5);
