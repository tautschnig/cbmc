let found: number = -1;
outer: for (let i = 0; i < 5; i++) {
  for (let j = 0; j < 5; j++) {
    if (i === 2 && j === 3) {
      found = i * 10 + j;
      break outer;
    }
  }
}
console.assert(found === 23);
