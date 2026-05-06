function fib(n: number): number {
  let a: number = 0;
  let b: number = 1;
  for (let i = 0; i < n; i++) {
    const t: number = a + b;
    a = b;
    b = t;
  }
  return a;
}
console.assert(fib(0) === 0);
console.assert(fib(1) === 1);
console.assert(fib(7) === 13);
