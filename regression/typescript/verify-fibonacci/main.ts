function fib(n: number): number {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}
console.assert(fib(0) === 0);
console.assert(fib(1) === 1);
console.assert(fib(5) === 5);
console.assert(fib(7) === 13);
