const arr: number[] = [10, 20, 30, 40, 50];
let sum: number = 0;
for (let i = 0; i < 5; i++) {
  sum += arr[i];
}
console.assert(sum === 150);
