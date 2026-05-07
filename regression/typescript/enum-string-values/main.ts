// TSH: Enums — string enum values are preserved through member access.
enum LogLevel { DEBUG = "debug", INFO = "info", ERROR = "error" }
console.assert(LogLevel.DEBUG === "debug");
console.assert(LogLevel.INFO === "info");
console.assert(LogLevel.ERROR === "error");

// Different instances of the same member compare equal.
const l1: string = LogLevel.DEBUG;
const l2: string = LogLevel.DEBUG;
console.assert(l1 === l2);

// Comparison between different members is false.
console.assert(LogLevel.DEBUG !== LogLevel.INFO);
console.assert(LogLevel.INFO !== LogLevel.ERROR);
