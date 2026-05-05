const double = (x: number): number => x * 2;
const increment = (x: number): number => x + 1;
function compose(f: (x: number) => number, g: (x: number) => number, x: number): number {
  return f(g(x));
}
console.assert(compose(double, increment, 3) === 8);
console.assert(compose(increment, double, 3) === 7);
