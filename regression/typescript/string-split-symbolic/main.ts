// ES2024 §22.1.3.24: String.prototype.split on a symbolic receiver.
// split("") on a symbolic string returns an array of single-char
// strings, one per position up to TYPESCRIPT_MAX_ARRAY_LENGTH.
const s: string = nondet_string();
__CPROVER_assume(s === "abc");
const parts: string[] = s.split("");
console.assert(parts.length === 3);
