// ES2024 sec-array.prototype.map
const arr: number[] = [1, 2, 3];
const doubled: number[] = arr.map((x: number): number => x * 2);
console.assert(doubled[0] === 2);
console.assert(doubled[1] === 4);
console.assert(doubled[2] === 6);
