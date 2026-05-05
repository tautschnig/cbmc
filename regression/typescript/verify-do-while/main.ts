let n: number = 1;
let count: number = 0;
do {
  n = n * 2;
  count = count + 1;
} while (n < 100);
console.assert(n === 128);
console.assert(count === 7);
