function makeAdder(base: number): (x: number) => number {
  return (x: number): number => x + base;
}
const add10 = makeAdder(10);
const add20 = makeAdder(20);
console.assert(add10(5) === 15);
console.assert(add20(5) === 25);
