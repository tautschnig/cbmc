const matrix: number[] = [1, 2, 3, 4, 5, 6, 7, 8, 9];
let found: number = -1;
outer: for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (matrix[i * 3 + j] === 5) {
      found = i * 3 + j;
      break outer;
    }
  }
}
console.assert(found === 4);
