function factorial(n: number): number {
  let result: number = 1;
  for (let i = 2; i <= n; i++) {
    result = result * i;
  }
  return result;
}
console.assert(factorial(5) === 120);
console.assert(factorial(0) === 1);
