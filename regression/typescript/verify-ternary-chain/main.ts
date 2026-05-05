function grade(score: number): number {
  return score >= 90 ? 4 : score >= 80 ? 3 : score >= 70 ? 2 : 0;
}
console.assert(grade(95) === 4);
console.assert(grade(85) === 3);
console.assert(grade(75) === 2);
console.assert(grade(50) === 0);
