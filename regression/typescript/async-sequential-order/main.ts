// Sequential await: strict ordering
let counter: number = 0;
async function step(): Promise<number> {
  counter = counter + 1;
  return counter;
}
const a: number = await step();
const b: number = await step();
const c: number = await step();
console.assert(a === 1);
console.assert(b === 2);
console.assert(c === 3);
console.assert(counter === 3);
