const arr: number[] = [1, 2, 3];
const idx: number = arr.findIndex((x: number): boolean => x > 10);
console.assert(idx === -1);
