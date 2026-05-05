function assertPositive(x: number): void {
  if (x <= 0) throw new Error("must be positive");
}
assertPositive(5);
console.assert(true);
