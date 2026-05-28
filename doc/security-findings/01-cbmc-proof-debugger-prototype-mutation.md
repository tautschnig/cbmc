# Finding 1: cbmc-proof-debugger prototype mutation in trace parser

**Project**: model-checking/cbmc-proof-debugger
**Severity**: Medium (DoS / data corruption on attacker-controlled
trace input)
**Status**: New (not previously reported as far as we can tell)
**Found by**: CBMC TypeScript frontend +
`__CPROVER_assert_safe_property_key`

## Summary

The proof-debugger's variable-name parser accepts `__proto__`,
`constructor`, and `prototype` as ordinary identifiers. When a
malicious trace file contains a variable expression like
`obj.__proto__.polluted`, the parser produces a `Member` chain
whose `.field` strings include `__proto__`. The downstream
`StructValue.set` / `Value.fromPathAndValue` perform
`this.fields[elem.field] = newVal`, which (because `__proto__` is
a JavaScript magic accessor) replaces the prototype chain of
`this.fields` with an arbitrary `Value` object.

This corrupts the struct's `fields` map: `for..in` (used by
`toList`) now enumerates the inherited `Value` object's own
properties as if they were struct fields. `toSummary()` then
crashes with `TypeError: field.value.toSummary is not a function`,
killing the proof-debugger's display of the trace.

## Source pointers

- `src/variableLexer.ts` — identifier regex `\w[\w$]*` accepts
  `__proto__`, `constructor`, `prototype`.
- `src/variableParser.ts` line 60 — `new Member(tokens[1].value)`
  stores the raw identifier as `Member.field`.
- `src/value.ts` line 61 — `mem.fields[elem.field] = val;` (in
  `Value.fromPathAndValue`).
- `src/value.ts` line 191 — `this.fields[field[0]] = field[1];`
  (in `StructValue` constructor).
- `src/value.ts` `StructValue.set` — `this.fields[elem.field] = newVal;`
- `src/value.ts` `StructValue.toList` — `for (let name in this.fields)`
  enumerates inherited keys after pollution.

## Threat model

A developer opens a CBMC proof project that contains a trace file
crafted by an attacker (e.g. a malicious GitHub repository shared
for review, or a supply-chain compromise of a CBMC viewer output).
The proof-debugger loads the trace, parses the malicious variable,
mutates the prototype of an internal map, and then crashes when
displaying it.

The immediate impact is denial-of-service against the extension,
plus potential data corruption (display of incorrect trace
information). It is not classical Object.prototype global
pollution — only the local `fields` map is affected — but the
side effects (`for..in` iterating prototype properties) are still
exploitable.

## Reproducer

Vendored upstream sources in `/tmp/cbmc-debugger-repro` (this
session); the script `repro.ts` runs the unmodified
`variableParser`, `valueParser`, and `value.ts`:

```
$ node repro.js
Parsed path: [
  Member { field: 'obj' },
  Member { field: '__proto__' },
  Member { field: 'polluted' }
]
Object.getPrototypeOf(obj.fields): StructValue { ... }

$ node crash-probe.js
Calling obj.toSummary():
  CRASH: field.value.toSummary is not a function
```

CBMC harness: `regression/typescript-real-world/cbmc-debugger-parser-proto/main.ts`.
The harness replays the parser logic and fails the
`__CPROVER_assert_safe_property_key` check on the parsed
`Member.field`, demonstrating that CBMC's primitive detects this
before the upstream's runtime crash.

## Suggested fix

Reject `__proto__`, `constructor`, and `prototype` as identifiers
in the variable lexer:

```typescript
// In MemberParser.parser, before constructing Member:
const dangerous = ['__proto__', 'constructor', 'prototype'];
if (dangerous.includes(tokens[1].value)) {
    return undefined;  // or throw
}
return [new Member(tokens[1].value), tokens.slice(2)];
```

Or, change `StructValue.fields` to use a `Map<string, Value>`
instead of a plain object so that `__proto__` becomes a regular
key with no prototype-chain side effects.

## Disclosure

Not yet reported upstream. Recommend filing privately before
public disclosure.
