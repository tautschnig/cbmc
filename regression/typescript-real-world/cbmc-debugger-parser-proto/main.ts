// Real-world security analysis: cbmc-proof-debugger value/variable parsers.
//
// The proof-debugger parses CBMC trace files and walks the resulting
// path structure to populate a memory map. The path elements come
// from a tokenized variable expression like "obj.foo[0]". The
// variableLexer accepts any identifier matching \w[\w$]*, which
// includes "__proto__", "constructor", and "prototype".
//
// Subsequent code uses the parsed name as a property key in
// StructValue:
//   fromPathAndValue:  mem.fields[elem.field] = val;
//   set:               this.fields[elem.field] = newVal;
//
// If a malicious trace file contains a variable like
// "obj.__proto__.foo", the parsed Member has field="__proto__"
// and the assignment mutates the prototype chain of `fields`.
// This is a real-world prototype-pollution-class issue.
//
// Source: github.com/model-checking/cbmc-proof-debugger
//   src/variableLexer.ts  (identifier pattern)
//   src/variableParser.ts (MemberParser)
//   src/value.ts          (StructValue.set, fromPathAndValue)

// Mirrors variableLexer's identifier regex: \w[\w$]*
function isValidIdentifier(s: string): boolean {
    if (s.length === 0) return false;
    const c0 = s.charCodeAt(0);
    const isWord = (c: number) =>
        (c >= 0x41 && c <= 0x5a) ||  // A-Z
        (c >= 0x61 && c <= 0x7a) ||  // a-z
        (c >= 0x30 && c <= 0x39) ||  // 0-9
        c === 0x5f;                  // _
    if (!isWord(c0)) return false;
    for (let i = 1; i < s.length; i++) {
        const c = s.charCodeAt(i);
        if (!isWord(c) && c !== 0x24 /* $ */) return false;
    }
    return true;
}

// Mirrors variableParser.MemberParser: extracts identifier as
// Member.field.
function parseMemberField(token: string): string {
    return token;  // direct, no filtering — matches upstream
}

// Mirrors the property-assignment sink in StructValue.set /
// fromPathAndValue. Asserts the field is a safe property key.
function safeSetField(field: string): void {
    __CPROVER_assert_safe_property_key(field);
}

function harness(): void {
    const maliciousField: string = "__proto__";
    console.assert(isValidIdentifier(maliciousField));
    const memberField: string = parseMemberField(maliciousField);
    safeSetField(memberField);
}
harness();
