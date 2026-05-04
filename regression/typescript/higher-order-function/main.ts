function apply(f: (x: number) => number, val: number): number {
  return f(val);
}
const double = (x: number): number => x * 2;
console.assert(apply(double, 5) === 10);
