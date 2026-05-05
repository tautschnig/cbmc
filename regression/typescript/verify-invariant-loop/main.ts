// Loop invariant: sum === i*(i+1)/2 (verified at end)
let sum: number = 0;
for (let i = 1; i <= 5; i++) {
  sum = sum + i;
}
console.assert(sum === 15); // 5*6/2
