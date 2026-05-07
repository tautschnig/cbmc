async function double(x: number): Promise<number> {
  return x * 2;
}
async function main(): Promise<void> {
  const a: number = await double(5);
  const b: number = await double(10);
  console.assert(a === 10);
  console.assert(b === 20);
}
await main();
