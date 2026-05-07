// KNOWNBUG: String enum values aren't preserved through member access.
// Numeric enums work. TSH: Enums
enum LogLevel { DEBUG = "debug", INFO = "info", ERROR = "error" }
console.assert(LogLevel.DEBUG === "debug");
