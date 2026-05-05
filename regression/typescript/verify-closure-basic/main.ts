function makeAdder(n: number): (x: number) => number {
  return (x: number): number => x + n;
}
const add5 = makeAdder(5);
console.assert(add5(3) === 8);
console.assert(add5(10) === 15);
