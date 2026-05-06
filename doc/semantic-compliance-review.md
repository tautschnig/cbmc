# Semantic Compliance Review: ES2024 & TypeScript Handbook

## Review Date: 2026-05-06
## Status: 423 CORE, 3 KNOWNBUG, 113 cross-references

---

## Compliance Gaps Found

### 1. Number semantics (ES2024 sec-ecmascript-language-types-number-type)

**Spec:** All numbers are IEEE 754 double-precision floats.
**Our impl:** Mostly correct (floatbv[64]), BUT integer inference changes
some variables to signedbv[32/64]. This is a **sound overapproximation**
for verification (integers are a subset of floats for integer values),
but could miss bugs related to:
- Integer overflow (signedbv[32] wraps at ±2^31, floats don't)
- NaN propagation (integers can't be NaN)
- Infinity (integers can't be infinite)

**Action:** Document that integer inference is an optimization that
trades precision for performance. Add `--no-integer-inference` flag
to disable it when full float semantics are needed.

### 2. String comparison (ES2024 sec-abstract-relational-comparison)

**Spec:** String comparison is lexicographic by UTF-16 code units.
**Our impl:** String equality uses structural comparison of
refined_string_typet (length + content pointer). This works for
constant strings but NOT for:
- Non-constant string comparison (needs string solver)
- Relational comparison (<, >, <=, >=) on strings
- String coercion in comparisons

**Action:** Blocked on string solver. Document limitation.

### 3. typeof operator (ES2024 sec-typeof-operator)

**Spec:** Returns "number", "string", "boolean", "undefined",
"object", "function", "symbol", "bigint".
**Our impl:** Returns constant strings for known types. Correct for
number/string/boolean. Missing: "object" for null (spec quirk),
"function" for callables, "undefined" for undefined.

**Action:** Add "object" return for null, "function" for function types.

### 4. Equality operators (ES2024 sec-abstract-equality-comparison)

**Spec:** `==` performs type coercion; `===` is strict.
**Our impl:** Both `==` and `===` use the same comparison (no coercion).
This is **incorrect** for `==` but acceptable for verification since
TypeScript's strict mode discourages `==`.

**Action:** Low priority. Document that `==` is treated as `===`.

### 5. Array.prototype.filter (ES2024 sec-array.prototype.filter)

**Spec:** Returns a new array. The callback receives (element, index, array).
**Our impl:** Callback only receives (element). Index parameter is not
passed to filter callbacks.

**Action:** Add index parameter to filter callback calls (like map does).

### 6. Array.prototype.reduce (ES2024 sec-array.prototype.reduce)

**Spec:** Callback receives (accumulator, currentValue, currentIndex, array).
**Our impl:** Callback receives (accumulator, currentValue) only.

**Action:** Low priority — index/array params rarely used in reduce.

### 7. Array mutating methods (ES2024 sec-array.prototype.push, pop, splice)

**Spec:** push/pop/splice mutate the array in-place and return values.
**Our impl:** push appends (correct), pop returns last element (partially
correct — doesn't update length in all cases). splice not implemented.

**Action:** Verify pop updates length. Add splice.

### 8. for...of loops (ES2024 sec-for-in-and-for-of-statements)

**Spec:** Iterates over iterable objects using Symbol.iterator protocol.
**Our impl:** Unrolls for constant arrays only. Doesn't support:
- Iterating over Map/Set
- Generator functions
- Custom iterables

**Action:** Low priority for verification. Document limitation.

### 9. Destructuring (ES2024 sec-destructuring-assignment)

**Spec:** Supports array destructuring, object destructuring, defaults,
rest elements, nested patterns.
**Our impl:** Array destructuring with rest works. Object destructuring
is property-access based (not true destructuring). Missing:
- Nested destructuring: `const {a: {b}} = obj`
- Default values in destructuring: `const {x = 0} = obj`
- Computed property destructuring: `const {[key]: val} = obj`

**Action:** Medium priority. Add nested object destructuring.

### 10. Class features (ES2024 sec-class-definitions)

**Spec:** Classes support: constructor, methods, static methods,
getters/setters, private fields (#field), computed property names,
extends, super.
**Our impl:** Constructor, methods, static, getters/setters, extends,
super all work. Missing:
- Private fields (#field)
- Computed property names in classes
- Abstract classes (TSH)
- Method overloading (TSH)

**Action:** Low priority. Private fields are a TypeScript-only concern.

### 11. Closure semantics (ES2024 sec-execution-contexts)

**Spec:** Closures capture variables by reference. Mutations to captured
variables are visible to all closures sharing the same scope.
**Our impl:** Closures capture by value (extra parameter). The
closure-counter test works because the captured symbol is shared, but
this is coincidental — it only works for single-closure cases.

**Action:** Document that multi-closure sharing (two closures capturing
the same mutable variable) is not supported.

### 12. Promise (ES2024 sec-promise-objects)

**Spec:** Promises are asynchronous. then/catch/finally chain handlers.
**Our impl:** Promise<T> is treated as T (synchronous). Promise.resolve
returns the value directly. No support for:
- then/catch/finally
- Promise.all/race/any
- Async iteration

**Action:** Acceptable for verification (async is modeled as sync).
Document limitation.

### 13. Spread operator (ES2024 sec-runtime-semantics-arrayaccumulation)

**Spec:** Spread in array literals copies all elements from iterable.
Spread in object literals copies own enumerable properties.
**Our impl:** Array spread works for constant and runtime arrays.
Object spread works with property override (rightmost wins). Missing:
- Spread of non-array iterables (Map, Set, generators)
- Spread in function call arguments: `fn(...args)`

**Action:** Add spread in function call arguments.

---

## TypeScript Handbook Gaps

### TSH: Generics
Not implemented. Generic functions and classes are handled by stripping
type parameters. No generic instantiation or type parameter constraints.

### TSH: Type Guards (user-defined)
Only `typeof` narrowing is implemented. Missing:
- `in` operator narrowing
- User-defined type guards (`x is Type`)
- Discriminated unions (tag-based narrowing)

### TSH: Utility Types
None implemented: Partial<T>, Required<T>, Pick<T,K>, Omit<T,K>,
Record<K,V>, Exclude<T,U>, Extract<T,U>, etc.

### TSH: Decorators
Not implemented. Low priority for verification.

### TSH: Namespaces
Not implemented. Modules (import/export) are supported instead.

---

## Priority Work Items (from this review)

1. **Add `--no-integer-inference` flag** — allow disabling for full float semantics
2. **typeof returns "function" for function types** — spec compliance
3. **Filter callback index parameter** — pass (elem, idx) not just (elem)
4. **Spread in function call arguments** — `fn(...args)`
5. **Document `==` treated as `===`** — in verification guide
6. **Nested object destructuring** — `const {a: {b}} = obj`
7. **Multi-closure mutable sharing** — document limitation

---

## Full Work Item List (from 13 gaps)

### Actionable now:
1. Add `--no-integer-inference` flag
2. typeof returns "function" for function types
3. Filter callback index parameter
4. Spread in function call arguments `fn(...args)`
5. Nested object destructuring
6. Verify pop updates length, add Array.prototype.splice
7. Reduce callback index parameter

### Documentation only:
8. Document `==` treated as `===`
9. Document multi-closure mutable sharing limitation

### Deferred (blocked or low priority):
10. String comparison with non-constant values (blocked: string solver)
11. for...of on Map/Set/generators (low priority)
12. Private fields (#field) (low priority, TS-only)
13. Promise then/catch/finally (acceptable: sync model)
