function makeAcc(): (x: number) => number {
  let total: number = 0;
  return (x: number): number => { total = total + x; return total; };
}
const acc = makeAcc();
console.assert(acc(5) === 5);
console.assert(acc(3) === 8);
console.assert(acc(2) === 10);
