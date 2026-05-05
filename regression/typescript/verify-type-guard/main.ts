function processValue(x: number | string): number {
  if (typeof x === "number") {
    return x + 1;
  }
  return x.length;
}
console.assert(processValue(10) === 11);
console.assert(processValue("abc") === 3);
