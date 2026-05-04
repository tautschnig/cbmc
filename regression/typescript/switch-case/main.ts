// ES2024 sec-switch-statement: The switch Statement
function classify(x: number): number {
  switch (x) {
    case 1: return 10;
    case 2: return 20;
    case 3: return 30;
    default: return 0;
  }
}
console.assert(classify(1) === 10);
console.assert(classify(3) === 30);
console.assert(classify(99) === 0);
