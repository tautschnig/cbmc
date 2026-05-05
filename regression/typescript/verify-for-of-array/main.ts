const arr: number[] = [10, 20, 30];
let sum: number = 0;
for (const x of arr) {
  sum = sum + x;
}
console.assert(sum === 60);
