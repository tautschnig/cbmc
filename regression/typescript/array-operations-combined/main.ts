const arr: number[] = [5, 3, 8, 1, 9, 2, 7];
console.assert(arr.length === 7);
console.assert(arr.includes(8) === true);
console.assert(arr.includes(10) === false);
console.assert(arr.indexOf(9) === 4);
console.assert(arr.findIndex((x: number): boolean => x > 7) === 2);
