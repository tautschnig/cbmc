function collatzSteps(n: number): number {
  let steps: number = 0;
  while (n !== 1) {
    if (n % 2 === 0) n = n / 2;
    else n = 3 * n + 1;
    steps++;
  }
  return steps;
}
console.assert(collatzSteps(6) === 8);
