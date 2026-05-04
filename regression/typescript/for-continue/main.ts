// ES2024 sec-continue-statement
let sum: number = 0;
for (let i = 0; i < 10; i++) {
  if (i % 2 === 0) continue;
  sum += i;
}
console.assert(sum === 25); // 1+3+5+7+9
