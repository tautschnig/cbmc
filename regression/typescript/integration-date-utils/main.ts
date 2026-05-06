// Simplified date-fns-like utilities
function daysInMonth(month: number, year: number): number {
  if (month === 2) {
    if (year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0)) return 29;
    return 28;
  }
  if (month === 4 || month === 6 || month === 9 || month === 11) return 30;
  return 31;
}

function isLeapYear(year: number): boolean {
  return year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
}

function addDays(day: number, month: number, n: number): number {
  return day + n;
}

console.assert(daysInMonth(1, 2024) === 31);
console.assert(daysInMonth(2, 2024) === 29);
console.assert(daysInMonth(2, 2023) === 28);
console.assert(daysInMonth(4, 2024) === 30);
console.assert(isLeapYear(2024) === true);
console.assert(isLeapYear(2023) === false);
console.assert(isLeapYear(2000) === true);
console.assert(isLeapYear(1900) === false);
console.assert(addDays(10, 1, 5) === 15);
