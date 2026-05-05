const arr: number[] = [0, 1, 2, 3, 4];
const inc: number[] = arr.map((x: number): number => x + 1);
console.assert(inc[0] === 1);
console.assert(inc[4] === 5);
