function isNumber(x: number | string): x is number {
  return typeof x === "number";
}
const v: number | string = 42;
if (isNumber(v)) {
  console.assert(v === 42);
}
