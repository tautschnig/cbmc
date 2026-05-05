// Array.from pattern: create array with computed values
const arr: number[] = [0, 1, 2, 3, 4].map((i: number): number => i * i);
console.assert(arr[0] === 0);
console.assert(arr[1] === 1);
console.assert(arr[2] === 4);
console.assert(arr[3] === 9);
console.assert(arr[4] === 16);
