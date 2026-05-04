// ES2024 sec-do-while-statement
let x: number = 0;
let i: number = 0;
do {
  x += i;
  i++;
} while (i < 5);
console.assert(x === 10); // 0+1+2+3+4
