const arr: number[] = [-3, -1, 0, 2, 4, 6];
const pos: number[] = arr.filter((x: number): boolean => x > 0);
console.assert(pos.length === 3);
