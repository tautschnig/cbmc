const words: number[] = [100, 200, 300];
const withIndex: number[] = words.map((val: number, idx: number): number => val + idx * 10);
console.assert(withIndex[0] === 100);
console.assert(withIndex[1] === 210);
console.assert(withIndex[2] === 320);
