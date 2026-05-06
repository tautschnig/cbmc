let total: number = 0;
for (let i = 1; i <= 3; i++) {
  for (let j = 1; j <= 3; j++) {
    total = total + i * j;
  }
}
console.assert(total === 36); // (1+2+3)*(1+2+3) = 36
