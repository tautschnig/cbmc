// ES2024 sec-for-in-and-for-of-statements: for-of
const arr: number[] = [10, 20, 30];
let sum: number = 0;
for (const x of arr) {
  sum += x;
}
console.assert(sum === 60);
