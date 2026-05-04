// Verify array is sorted after manual sort
const arr: number[] = [3, 1, 2];
// Bubble sort one pass
if (arr[0] > arr[1]) { const t = arr[0]; arr[0] = arr[1]; arr[1] = t; }
if (arr[1] > arr[2]) { const t = arr[1]; arr[1] = arr[2]; arr[2] = t; }
if (arr[0] > arr[1]) { const t = arr[0]; arr[0] = arr[1]; arr[1] = t; }
console.assert(arr[0] <= arr[1]);
console.assert(arr[1] <= arr[2]);
