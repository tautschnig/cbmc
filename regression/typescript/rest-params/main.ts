// ES2024 sec-function-definitions: rest parameters
function sum(...nums: number[]): number {
  let total: number = 0;
  for (const n of nums) { total += n; }
  return total;
}
console.assert(sum(1, 2, 3) === 6);
