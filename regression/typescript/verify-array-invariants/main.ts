const arr: number[] = [1, 2, 3, 4, 5];
let sum: number = 0;
for (let i = 0; i < arr.length; i++) sum = sum + arr[i];
const avg: number = sum / arr.length;
console.assert(sum === 15);
console.assert(arr.length === 5);
console.assert(avg === 3);
