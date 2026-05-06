async function compute(): Promise<number> {
  return 42;
}
const val: number = await compute();
console.assert(val === 42);
const p: number = await Promise.resolve(10);
console.assert(p === 10);
