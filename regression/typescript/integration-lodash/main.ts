// Simplified lodash-like utilities
function chunk(arr: number[], size: number): number[] {
  const result: number[] = [];
  for (let i = 0; i < arr.length; i = i + size) {
    for (let j = 0; j < size && i + j < arr.length; j++) {
      result.push(arr[i + j]);
    }
  }
  return result;
}

function zip(a: number[], b: number[]): number[] {
  const result: number[] = [];
  const len: number = a.length < b.length ? a.length : b.length;
  for (let i = 0; i < len; i++) {
    result.push(a[i] + b[i]);
  }
  return result;
}

const arr: number[] = [1, 2, 3, 4];
const chunked: number[] = chunk(arr, 2);
console.assert(chunked.length === 4);
console.assert(chunked[0] === 1);

const a: number[] = [1, 2, 3];
const b: number[] = [10, 20, 30];
const zipped: number[] = zip(a, b);
console.assert(zipped[0] === 11);
console.assert(zipped[1] === 22);
console.assert(zipped[2] === 33);
