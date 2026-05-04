// ES2024 §23.1: Array Objects
const arr: number[] = [1, 2, 3];
console.assert(arr.length === 3);
console.assert(arr[0] === 1);
console.assert(arr[2] === 3);
arr.push(4);
console.assert(arr.length === 4);
console.assert(arr[3] === 4);
