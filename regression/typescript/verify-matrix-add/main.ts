const m1: number[] = [1, 2, 3, 4];
const m2: number[] = [5, 6, 7, 8];
const result: number[] = m1.map((x: number, i: number): number => x + m2[i]);
console.assert(result[0] === 6);
console.assert(result[3] === 12);
