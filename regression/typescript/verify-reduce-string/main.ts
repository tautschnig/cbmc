const nums: number[] = [1, 2, 3];
const sum: number = nums.reduce((acc: number, x: number): number => acc + x, 0);
console.assert(sum === 6);
