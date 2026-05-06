function contains(arr: number[], target: number): boolean {
  for (let i = 0; i < arr.length; i++) {
    if (arr[i] === target) return true;
  }
  return false;
}
const data: number[] = [1, 2, 3, 4, 5];
console.assert(contains(data, 3) === true);
console.assert(contains(data, 99) === false);
