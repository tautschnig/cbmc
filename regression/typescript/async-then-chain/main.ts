const result: number = await Promise.resolve(10)
  .then((x: number): number => x + 5)
  .then((x: number): number => x * 2)
  .then((x: number): number => x - 1);
console.assert(result === 29); // ((10+5)*2)-1
