let found: number = -1;
for (let i = 0; i < 5; i++) {
  for (let j = 0; j < 5; j++) {
    if (i * 5 + j === 13) {
      found = i * 5 + j;
      break;
    }
  }
}
console.assert(found === 13);
