function makeCounter(): (x: number) => number {
  let count: number = 0;
  function add(n: number): number {
    count = count + n;
    return count;
  }
  return add;
}
const counter = makeCounter();
console.assert(counter(5) === 5);
