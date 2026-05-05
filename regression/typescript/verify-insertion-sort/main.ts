function insertionSort(arr: number[]): number[] {
  const result: number[] = [...arr];
  for (let i = 1; i < result.length; i++) {
    const key: number = result[i];
    let j: number = i - 1;
    while (j >= 0 && result[j] > key) {
      result[j + 1] = result[j];
      j = j - 1;
    }
    result[j + 1] = key;
  }
  return result;
}
const data: number[] = [3, 1, 4, 1, 5];
const sorted: number[] = insertionSort(data);
console.assert(sorted[0] === 1);
