let sum: number = 0;
let i: number = 0;
while (i < 10) {
  sum += i;
  i++;
  __CPROVER_assert(sum >= 0);
  __CPROVER_assert(i <= 10);
}
console.assert(sum === 45);
