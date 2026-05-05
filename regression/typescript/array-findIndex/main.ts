const arr: number[] = [10, 20, 30, 40];
console.assert(arr.findIndex((x: number): boolean => x > 25) === 2);
console.assert(arr.findIndex((x: number): boolean => x > 100) === -1);
