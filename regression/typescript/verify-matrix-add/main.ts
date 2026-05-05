// Map with index parameter
const arr: number[] = [10, 20, 30, 40];
const indexed: number[] = arr.map((x: number, i: number): number => x + i);
console.assert(indexed[0] === 10); // 10 + 0
console.assert(indexed[1] === 21); // 20 + 1
console.assert(indexed[2] === 32); // 30 + 2
console.assert(indexed[3] === 43); // 40 + 3
