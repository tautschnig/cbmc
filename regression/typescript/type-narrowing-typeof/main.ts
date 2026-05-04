// TSH: Narrowing.md — typeof type guards
function double(x: number | string): number {
  if (typeof x === "number") {
    return x * 2;
  }
  return x.length;
}
console.assert(double(5) === 10);
console.assert(double("hi") === 2);
