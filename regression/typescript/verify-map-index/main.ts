const arr: number[] = [10, 20, 30, 40, 50];
const indexed: number[] = arr.map((val: number, idx: number): number => val + idx);
console.assert(indexed[0] === 10);
console.assert(indexed[1] === 21);
console.assert(indexed[4] === 54);
