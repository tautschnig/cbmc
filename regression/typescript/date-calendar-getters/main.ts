// ES2024 §21.4.1: Date calendar/time-of-day getters via integer
// modular arithmetic. Verifies that the getters return the correct
// values for known timestamps.
//
// All test timestamps are in UTC (the JS spec does NOT mandate a
// timezone for these getters in our model — we treat all times as
// UTC since the cbmc TypeScript model has no DST/timezone state).

function main(): void {
    // 1970-01-01T00:00:00.000 UTC — Unix epoch
    const epoch = new Date(0);
    console.assert(epoch.getMilliseconds() === 0);
    console.assert(epoch.getSeconds() === 0);
    console.assert(epoch.getMinutes() === 0);
    console.assert(epoch.getHours() === 0);
    console.assert(epoch.getDay() === 4);     // Thursday
    console.assert(epoch.getDate() === 1);    // 1st of the month
    console.assert(epoch.getMonth() === 0);   // January (0-indexed)

    // 2026-01-01T00:00:00.000 UTC = 1767225600000 ms
    const newYear2026 = new Date(1767225600000);
    console.assert(newYear2026.getMilliseconds() === 0);
    console.assert(newYear2026.getSeconds() === 0);
    console.assert(newYear2026.getMinutes() === 0);
    console.assert(newYear2026.getHours() === 0);
    console.assert(newYear2026.getDay() === 4);  // 2026-01-01 is Thursday
    console.assert(newYear2026.getDate() === 1);
    console.assert(newYear2026.getMonth() === 0);

    // 2026-05-29T17:42:55.123 UTC ≈ 1780076575123 ms
    const someTime = new Date(1780076575123);
    console.assert(someTime.getMilliseconds() === 123);
    console.assert(someTime.getSeconds() === 55);
    console.assert(someTime.getMinutes() === 42);
    console.assert(someTime.getHours() === 17);
    console.assert(someTime.getDay() === 5);  // 2026-05-29 is Friday
    console.assert(someTime.getDate() === 29);
    console.assert(someTime.getMonth() === 4);  // May = 4 (0-indexed)

    // 2024-02-29 is a leap day. 2024-02-29T12:00:00.000 UTC = 1709208000000
    const leapDay = new Date(1709208000000);
    console.assert(leapDay.getMilliseconds() === 0);
    console.assert(leapDay.getSeconds() === 0);
    console.assert(leapDay.getMinutes() === 0);
    console.assert(leapDay.getHours() === 12);
    console.assert(leapDay.getDate() === 29);
    console.assert(leapDay.getMonth() === 1);  // February
}
main();
