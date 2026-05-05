function classify(x: number, y: number): number {
  if (x > 0) {
    if (y > 0) return 1;
    else return 2;
  } else {
    if (y > 0) return 3;
    else return 4;
  }
}
console.assert(classify(1, 1) === 1);
console.assert(classify(1, -1) === 2);
console.assert(classify(-1, 1) === 3);
console.assert(classify(-1, -1) === 4);
