let count: number = 0;
outer: for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (j === 1) continue outer;
    count = count + 1;
  }
}
console.assert(count === 3);
