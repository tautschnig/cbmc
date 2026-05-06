const result: number = await Promise.resolve(5)
  .then((x: number): number => x * 2)
  .then((x: number): number => x + 1);
console.assert(result === 11);
