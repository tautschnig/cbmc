function grade(s: number): number {
  return s >= 90 ? 4 : s >= 80 ? 3 : s >= 70 ? 2 : s >= 60 ? 1 : 0;
}
console.assert(grade(95) === 4);
console.assert(grade(85) === 3);
console.assert(grade(50) === 0);
