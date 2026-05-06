// Fibonacci with Map-based memoization
function memoFib(n: number, cache: Map<number, number>): number {
  if (n <= 1) return n;
  if (cache.has(n)) return cache.get(n) as number;
  const result = memoFib(n - 1, cache) + memoFib(n - 2, cache);
  cache.set(n, result);
  return result;
}

const cache = new Map<number, number>();
console.assert(memoFib(5, cache) === 5);
