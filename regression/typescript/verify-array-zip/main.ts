const a: number[] = [1, 2, 3];
const b: number[] = [4, 5, 6];
const sums: number[] = a.map((x: number, i: number): number => x + b.at(i));
console.assert(sums[0] === 5);
console.assert(sums[1] === 7);
console.assert(sums[2] === 9);
