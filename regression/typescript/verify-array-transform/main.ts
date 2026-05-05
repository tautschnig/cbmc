const arr: number[] = [1, 2, 3, 4, 5];
const doubled: number[] = arr.map((x: number): number => x * 2);
const filtered: number[] = doubled.filter((x: number): boolean => x > 4);
console.assert(filtered.length === 3);
console.assert(filtered[0] === 6);
