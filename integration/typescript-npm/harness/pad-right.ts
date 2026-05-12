// Harness for npm package 'pad-right' (jonschlinkert/pad-right)
// Version tracked in integration/typescript-npm/package.json
//
// padRight(str, n, ch) right-pads str with ch until length is n.
// We verify LENGTH invariants (content-precise padEnd on symbolic
// receivers is not modelled; see the capability matrix).

function padRight(s: string, n: number, ch: string): string {
  if (s.length >= n) return s;
  return s.padEnd(n, ch);
}

// Property 1: result length equals max(input length, target length).
const s: string = "abc";
console.assert(padRight(s, 5, " ").length === 5);
console.assert(padRight(s, 10, "x").length === 10);

// Property 2: if target length <= input length, returns input unchanged.
console.assert(padRight(s, 3, " ") === "abc");
console.assert(padRight(s, 2, " ") === "abc");

// Property 3: length after padding matches target (when target > input).
const t: string = nondet_string();
__CPROVER_assume(t.length === 2);
console.assert(padRight(t, 5, "_").length === 5);
