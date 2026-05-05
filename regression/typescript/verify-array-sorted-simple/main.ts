const arr: number[] = [1, 3, 5, 7, 9];
// Verify array is sorted
for (let i = 0; i < 4; i++) {
  __CPROVER_assert(arr[i] <= arr[i + 1]);
}
