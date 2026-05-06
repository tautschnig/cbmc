const arr: number[] = [1, 2, 3, 4, 5];
let sum: number = 0;
for (let i = 0; i < arr.length; i++) {
  sum = sum + arr[i];
}
console.assert(sum === 15);
