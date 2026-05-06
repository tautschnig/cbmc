const val: number = await Promise.resolve(10).catch((e: number): number => 0);
console.assert(val === 10); // catch not triggered in sync model
