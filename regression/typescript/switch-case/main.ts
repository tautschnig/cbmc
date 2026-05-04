// ES2024 sec-switch-statement: The switch Statement
function dayName(day: number): string {
  switch (day) {
    case 1: return "Monday";
    case 2: return "Tuesday";
    case 3: return "Wednesday";
    default: return "Unknown";
  }
}
console.assert(dayName(1) === "Monday");
console.assert(dayName(3) === "Wednesday");
console.assert(dayName(99) === "Unknown");
