function dayType(day: number): number {
  switch (day) {
    case 0: return 0;
    case 6: return 0;
    case 1: return 1;
    case 2: return 1;
    default: return -1;
  }
}
console.assert(dayType(0) === 0);
console.assert(dayType(6) === 0);
console.assert(dayType(1) === 1);
console.assert(dayType(7) === -1);
