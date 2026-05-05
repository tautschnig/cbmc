enum Day { Mon = 1, Tue = 2, Wed = 3, Thu = 4, Fri = 5 }
function isWeekday(d: Day): boolean { return d >= Day.Mon && d <= Day.Fri; }
console.assert(isWeekday(Day.Mon) === true);
console.assert(isWeekday(Day.Fri) === true);
