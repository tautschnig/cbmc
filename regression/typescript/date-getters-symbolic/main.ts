// ES2024 §21.4.1: Date getters preserve their bounded-range
// invariants even on symbolic timestamps.
//
// This test verifies that for any (non-negative) timestamp, the
// getter return values lie within their spec-mandated ranges. The
// previous nondet model would also satisfy these (vacuously); the
// new precise model satisfies them by construction.

function main(): void {
    // Constrain to a non-negative bounded timestamp.
    const ms_input = nondet_number();
    if (ms_input < 0 || ms_input >= 200000000000000) return;

    const d = new Date(ms_input);

    // Range invariants from ES2024 §21.4.1.
    const millis = d.getMilliseconds();
    console.assert(millis >= 0 && millis <= 999, "ms in [0, 999]");

    const sec = d.getSeconds();
    console.assert(sec >= 0 && sec <= 59, "sec in [0, 59]");

    const min = d.getMinutes();
    console.assert(min >= 0 && min <= 59, "min in [0, 59]");

    const hour = d.getHours();
    console.assert(hour >= 0 && hour <= 23, "hour in [0, 23]");

    const dow = d.getDay();
    console.assert(dow >= 0 && dow <= 6, "day-of-week in [0, 6]");

    const dom = d.getDate();
    console.assert(dom >= 1 && dom <= 31, "day-of-month in [1, 31]");

    const month = d.getMonth();
    console.assert(month >= 0 && month <= 11, "month in [0, 11]");
}
main();
