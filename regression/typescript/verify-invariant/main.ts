// Verify loop maintains invariant
let sum: number = 0;
for (let i = 0; i < 5; i++) {
  sum += i;
  __CPROVER_assert(sum >= 0);
  __CPROVER_assert(sum <= 10);
}
console.assert(sum === 10);
