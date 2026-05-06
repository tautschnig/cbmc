const data: number[] = [1, 2, 3, 4, 5, 6, 7, 8, 9];
let found_i: number = -1;
let found_j: number = -1;
outer: for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (data[i * 3 + j] === 5) {
      found_i = i;
      found_j = j;
      break outer;
    }
  }
}
console.assert(found_i === 1);
console.assert(found_j === 1);
