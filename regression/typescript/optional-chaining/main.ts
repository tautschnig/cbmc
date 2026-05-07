// KNOWNBUG: Optional chaining (o?.p) doesn't short-circuit on
// missing struct-typed optional fields.
//
// ES2024 sec-optional-chains (§13.3.9).
//
// Our struct model defaults all optional fields to zero-valued
// structs, so o.x?.y returns 0 (from the default struct's zero y
// field) rather than undefined (NaN in our sentinel model).
//
// A proper fix requires distinguishing "field present with default"
// from "field not present", which means either:
//   - Adding an 'is_present' bit per optional struct field, or
//   - Representing optional struct fields as a pointer that can be
//     null (NaN sentinel), then deref on access.
//
// Both are substantial refactors. Leaving as KNOWNBUG for now.
// Note: the parser DOES recognize ?. and emits 'optional: true'
// in the AST; only the semantic handling is missing.

interface Obj { x?: { y?: number; }; }
const o: Obj = {};
const v: number | undefined = o.x?.y;
console.assert(v === undefined);
