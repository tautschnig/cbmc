# Plan: Map/Set Iteration Support

## Current State

Map and Set are modeled as structs with a single `size` field:
```
struct Map { size: number }
struct Set { size: number }
```

Operations `set()`, `get()`, `has()`, `delete()` are not implemented.
`for...of` on Map/Set is not supported.

## Target State

Model Map<K,V> and Set<T> with bounded storage (like arrays):
```
struct Map<K,V> {
  size: signedbv[64],
  keys: K[MAX_MAP_SIZE],
  values: V[MAX_MAP_SIZE]
}
struct Set<T> {
  size: signedbv[64],
  data: T[MAX_MAP_SIZE]
}
```

## Required Changes

### 1. Type Model (typescript_converter_call.cpp, convert() function)

Replace the current Map/Set registration with:
```cpp
// Map: { size, keys[N], values[N] }
struct_typet map_type;
map_type.components().push_back({"size", signedbv_typet{64}});
map_type.components().push_back({"keys", array_typet{...}});
map_type.components().push_back({"values", array_typet{...}});
```

**Challenge:** Map<K,V> is generic — K and V aren't known until instantiation.
**Solution:** Defer type creation until the first `new Map<K,V>()` is seen.
Register a "template" and instantiate per-use.

**Simpler alternative:** Use `double_type()` for both keys and values
(covers the common `Map<string, number>` case with string keys modeled
as their hash/index).

### 2. Method Implementations

#### map.set(key, value)
```
keys[size] = key;
values[size] = value;
size++;
```
Note: doesn't handle duplicate keys (overapproximation — sound but imprecise).

#### map.get(key)
For constant keys: linear scan of keys[], return matching value.
For non-constant keys: return nondet (sound overapproximation).

#### map.has(key)
For constant keys: linear scan, return true/false.
For non-constant keys: return nondet_boolean().

#### map.delete(key)
Decrement size. Don't actually remove (overapproximation).

#### set.add(value)
```
data[size] = value;
size++;
```

#### set.has(value)
Same as map.has.

### 3. for...of Iteration

`for (const [key, value] of map)` needs to:
1. Unroll the loop up to `MAX_MAP_SIZE` iterations
2. For each iteration i: key = map.keys[i], value = map.values[i]
3. Guard: only execute body if i < map.size

`for (const item of set)` similarly:
1. Unroll up to MAX_MAP_SIZE
2. item = set.data[i], guarded by i < set.size

**Implementation location:** `convert_statement` ForOfStatement handler.
Currently only handles arrays. Extend to detect Map/Set types.

### 4. Constants and Limits

Add `TYPESCRIPT_MAX_MAP_SIZE` (default: 8, configurable via
`--ts-max-map-size`).

### 5. Complexity Estimate

| Component | Lines | Difficulty |
|-----------|-------|-----------|
| Type model | ~40 | Low |
| set/add methods | ~30 | Low |
| get/has methods | ~60 | Medium (linear scan) |
| delete method | ~15 | Low |
| for...of on Map | ~50 | Medium |
| for...of on Set | ~30 | Low |
| Generic instantiation | ~40 | Medium |
| Tests | ~50 | Low |
| **Total** | **~315** | **Medium** |

### 6. Test Cases

```typescript
// Map basic operations
const m = new Map<string, number>();
m.set("a", 1);
m.set("b", 2);
assert(m.size === 2);
assert(m.get("a") === 1);
assert(m.has("b") === true);

// Set basic operations
const s = new Set<number>();
s.add(1); s.add(2); s.add(3);
assert(s.size === 3);
assert(s.has(2) === true);

// Map iteration
let sum = 0;
for (const [key, value] of m) { sum += value; }
assert(sum === 3);
```

### 7. Risks

- Generic type instantiation adds complexity
- String keys require string solver for get/has with non-constant keys
- Duplicate key handling (set should overwrite) requires linear scan
- Performance: unrolling for...of with MAX_MAP_SIZE iterations

### 8. Recommended Approach

Phase 1 (immediate): Implement set/add with size tracking. get/has
return nondet for non-constant keys. No iteration.

Phase 2 (later): Add for...of iteration with loop unrolling.

Phase 3 (with string solver): Implement get/has for constant string keys.
