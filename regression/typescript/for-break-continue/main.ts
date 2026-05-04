// ES2024 sec-break-statement, sec-continue-statement
let sum: number = 0;
for (let i = 0; i < 10; i++) {
  if (i === 5) break;
  sum += i;
}
console.assert(sum === 10); // 0+1+2+3+4
