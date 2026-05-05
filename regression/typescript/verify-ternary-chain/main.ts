function grade(score: number): number {
  return score >= 90 ? 4 : score >= 80 ? 3 : score >= 70 ? 2 : score >= 60 ? 1 : 0;
}
console.assert(grade(95) === 4);
console.assert(grade(85) === 3);
console.assert(grade(55) === 0);
