let sum: number = 0;
let i: number = 1;
while (i <= 10) {
  sum = sum + i;
  i = i + 1;
}
console.assert(sum === 55);
