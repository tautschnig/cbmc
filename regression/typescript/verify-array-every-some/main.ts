const nums: number[] = [2, 4, 6, 8, 10];
console.assert(nums.every((x: number): boolean => x % 2 === 0) === true);
console.assert(nums.some((x: number): boolean => x > 9) === true);
console.assert(nums.some((x: number): boolean => x > 10) === false);
