// ES2024 §14.7.3: The while Statement
let sum: number = 0;
let i: number = 1;
while (i <= 10) {
  sum += i;
  i++;
}
console.assert(sum === 55);
