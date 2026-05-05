const arr: number[] = [1, 2, 3, 4, 5];
const doubled: number[] = arr.map((x: number): number => x * 2);
console.assert(doubled[0] === 2);
console.assert(doubled[2] === 6);
console.assert(doubled[4] === 10);
console.assert(doubled.length === 5);
