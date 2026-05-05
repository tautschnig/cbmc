function isPrime(n: number): boolean {
  if (n < 2) return false;
  for (let i = 2; i * i <= n; i++) {
    if (n % i === 0) return false;
  }
  return true;
}
console.assert(isPrime(7) === true);
console.assert(isPrime(4) === false);
console.assert(isPrime(13) === true);
