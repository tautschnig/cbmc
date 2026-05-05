const arr: number[] = [1, 2, 3, 4, 5];
// Check no duplicates in first 5 elements
let unique: boolean = true;
for (let i = 0; i < 4; i++) {
  for (let j = i + 1; j < 5; j++) {
    if (arr[i] === arr[j]) unique = false;
  }
}
console.assert(unique === true);
