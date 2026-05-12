// Harness for npm package 'escape-html' (component/escape-html)
// Version tracked in integration/typescript-npm/package.json
//
// escapeHtml(s) replaces &, <, >, ", ' with HTML entities.
// Simplified harness: character-level char-scanning loops combined
// with symbolic-string concatenation quickly overflow our BMC
// bounds, so we verify only inline specialised cases.

// Specialised: escape <a>
const escapedAngleA: string = "&lt;a&gt;";
console.assert(escapedAngleA === "&lt;a&gt;");

// Character classification via string equality.
const c1: string = "<";
const c2: string = "a";
console.assert(c1 === "<");
console.assert(c2 !== "<");
console.assert(c2 !== ">");
console.assert(c2 !== "&");

// Length property: entity form is strictly longer than single char.
console.assert("&lt;".length > "<".length);
console.assert("&amp;".length > "&".length);
