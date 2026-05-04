let sum: number = 0;
for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    sum += 1;
  }
}
console.assert(sum === 9);
