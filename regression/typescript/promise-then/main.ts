async function getValue(): Promise<number> { return 21; }
const p: number = await getValue();
const doubled: number = await Promise.resolve(p).then((x: number): number => x * 2);
console.assert(doubled === 42);
