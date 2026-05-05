// 400th CORE test
function factorial(n: number): number {
  if (n <= 1) return 1;
  return n * factorial(n - 1);
}
console.assert(factorial(1) === 1);
console.assert(factorial(3) === 6);
console.assert(factorial(5) === 120);
