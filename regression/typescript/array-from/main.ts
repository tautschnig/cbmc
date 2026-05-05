const original: number[] = [1, 2, 3, 4, 5];
const copy: number[] = Array.from(original);
console.assert(copy.length === 5);
console.assert(copy[0] === 1);
console.assert(copy[4] === 5);
const doubled: number[] = Array.from(original, (x: number, i: number): number => x * 2);
console.assert(doubled[0] === 2);
console.assert(doubled[2] === 6);
console.assert(doubled.length === 5);
