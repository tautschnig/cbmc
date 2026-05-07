function applyTax(amount: number, rate: number): number {
  return amount + amount * rate / 100;
}
console.assert(applyTax(100, 10) === 110);
console.assert(applyTax(200, 25) === 250);
console.assert(applyTax(50, 0) === 50);
