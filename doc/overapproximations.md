# Catalogue of Overapproximations in the Python Front-End

This document lists every point where the Python front-end produces a
sound overapproximation — i.e., where the GOTO model is less precise
than the actual Python semantics. An overapproximation means the verifier
may report "VERIFICATION FAILED" (false negative) but will never report
"VERIFICATION SUCCESSFUL" when the program actually has a bug (no false
positives for safety properties).

## 1. Bounded Data Structures

### 1.1 Strings bounded to 256 characters
- **What:** `python_string_type()` uses `data[256]` array
- **Impact:** Strings longer than 256 chars are truncated
- **PLR:** PLib stdtypes — "Strings are immutable sequences of Unicode code points"
- **Mitigation:** `--python-max-string-length` flag (not yet implemented)

### 1.2 Lists bounded to 64 elements
- **What:** `python_list_type()` uses `data[64]` array
- **Impact:** Lists longer than 64 elements overflow silently
- **PLR:** PLib stdtypes — "Lists are mutable sequences"
- **Mitigation:** `--python-max-list-length` flag (not yet implemented)

### 1.3 Integers bounded to 64 bits
- **What:** `python_int_type()` = `signedbv[64]`
- **Impact:** Overflow wraps around (Python ints have unlimited precision)
- **PLR:** PLR §3.2 — "Integers have unlimited precision"
- **Mitigation:** `--python-unbounded-ints --z3` uses `integer_typet`

### 1.4 Dicts bounded to literal keys
- **What:** Dicts modeled as structs with one field per literal key
- **Impact:** Dynamic key insertion/deletion not supported; `del` zeros value
- **PLR:** PLib stdtypes — "Dictionaries are mutable mappings"

## 2. Nondet Returns (Sound Overapproximation)

These return `side_effect_expr_nondett` — the solver treats the value as
unconstrained, which is always sound but may cause false negatives.

### 2.1 String methods returning transformed strings
- `str.upper()`, `str.lower()`, `str.strip()`, `str.title()`,
  `str.capitalize()`, `str.swapcase()`, `str.zfill()`, `str.casefold()`,
  `str.center()`, `str.ljust()`, `str.rjust()`, `str.expandtabs()`,
  `str.encode()`, `str.decode()`, `str.removeprefix()`, `str.removesuffix()`
- **Impact:** Content not tracked; length not preserved
- **PLR:** PLib stdtypes — String Methods

### 2.2 String methods returning nondet of correct type
- `str.replace()`, `str.format()`, `str.join()`, `str.partition()` → nondet string
- `str.find()`, `str.index()`, `str.rfind()`, `str.rindex()`, `str.count()` → nondet int
- `str.startswith()`, `str.endswith()`, `str.isalpha()`, `str.isdigit()`,
  `str.isalnum()`, `str.isspace()`, `str.isupper()`, `str.islower()`,
  `str.istitle()`, `str.isnumeric()`, `str.isdecimal()`, `str.isidentifier()`,
  `str.isprintable()`, `str.isascii()` → nondet bool
- `str.split()` with variable string/delimiter → nondet list of strings
- **Impact:** Can't prove content-dependent properties
- **PLR:** PLib stdtypes — String Methods

### 2.3 f-strings with formatted values
- `f"prefix{expr}suffix"` where `expr` is not a constant → nondet string
- All-constant f-strings (no `{}`) ARE tracked exactly
- **Impact:** Can't prove f-string content when expressions are involved
- **PLR:** PLR §2.4.3 — Formatted string literals

### 2.4 Built-in functions returning nondet
- `hex()`, `oct()`, `bin()`, `repr()`, `ascii()` → nondet string
- `hash()` → nondet int
- `input()` → nondet string
- `map(func, list)` → nondet list (exact would need unrolled function calls)
- `zip(a, b)` → nondet list
- `filter(func, list)` → nondet list
- **PLR:** PLib builtins

### 2.5 Module functions returning nondet
- `re.match()`, `re.search()`, `re.findall()`, etc. → nondet int
- `random.randint()`, `random.random()`, etc. → nondet int
- `math.sin()`, `math.cos()`, `math.tan()`, `math.log()`, `math.exp()` → nondet float
- `math.sqrt(x)` for non-constant `x` → nondet float (constant `x` computed exactly)
- **PLR:** PLib — various modules

### 2.6 Complex number operations
- `abs(complex(r, i))` for non-constant r, i → nondet float (constant computed exactly)
- `complex ** exponent` → nondet complex
- Other complex ops beyond +, -, * → nondet complex
- **PLR:** PLR §3.2 — Complex numbers

### 2.7 Unknown functions and methods
- Any unrecognized function → nondet int + "no body" property
- Any unrecognized method → nondet int
- **Impact:** Sound but imprecise; the "no body" property alerts the user

### 2.8 Type system fallbacks
- `safe_typecast(struct, scalar)` → nondet of target type
- `safe_typecast(scalar, struct)` → nondet of target type
- `wrap_value(class_struct)` → nondet `python_value_type`
- **Impact:** Information lost when types are incompatible

## 3. Tagged Union Imprecision

### 3.1 Truth value testing
- Tagged union → bool: checks INT (int_val!=0) and BOOL (bool_val)
- FLOAT, STR, LIST tags assumed truthy (may miss 0.0, "", [] falsiness)
- NONE tag → false (correct)
- **PLR:** PLib stdtypes — Truth Value Testing

### 3.2 Arithmetic on tagged unions
- `x + y` where both are `python_value_type`: extracts `__int_val` from both
- Loses type information if actual types are float or string
- **PLR:** PLR §6.7 — Binary arithmetic operations

### 3.3 String/list operations on tagged unions
- `len(x)` dispatches on tag: STR→deref(str_ptr).length, LIST→deref(list_ptr).length
- Other operations (indexing, slicing) don't dispatch — use int field

## 4. Semantic Simplifications

### 4.1 None modeled as sentinel value
- None = -4611686018427387904 (a large negative int)
- `x is None` checks `x == sentinel`
- **Risk:** A program that uses this exact integer value would be misidentified as None
- **PLR:** PLR §3.2 — None type

### 4.2 Exception type matching by hash
- Exception types identified by sum of character ASCII values
- `ZeroDivisionError` hash = 1775, `TypeError` hash = 940, etc.
- **Risk:** Hash collisions between different exception types (unlikely but possible)
- **PLR:** PLR §8.4 — The try statement

### 4.3 Class identity by struct tag
- `isinstance(obj, Class)` checks struct tag string, not runtime type
- Dynamic type changes (e.g., `obj.__class__ = OtherClass`) not supported
- **PLR:** PLR §6.10.2 — isinstance

### 4.4 Inheritance by struct layout
- Derived class structs include base class fields at same offsets
- `super().__init__()` inlines base class body (not a real function call)
- **Risk:** Multiple inheritance not supported; diamond inheritance would fail
- **PLR:** PLR §8.9 — Class definitions

### 4.5 del on dicts zeros value instead of removing key
- `del d["key"]` sets the value to zero, doesn't remove the field
- `len(d)` still counts the deleted key
- **PLR:** PLR §7.5 — The del statement

### 4.6 Missing return appends None sentinel
- Functions without explicit return on all paths get `return None` appended
- This is correct per PLR §7.6 but the None is our sentinel value, not Python's None object
- **PLR:** PLR §7.6 — The return statement

## 5. Unsupported Features (Silently Ignored)

### 5.1 Decorators (except @classmethod and @overload)
- `@staticmethod`, `@property`, custom decorators → ignored
- **PLR:** PLR §8.7 — Function definitions

### 5.2 Generators and yield
- `yield`, `yield from`, generator functions → not supported
- Generator expressions with variable iterables → nondet
- **PLR:** PLR §6.2.9 — Yield expressions

### 5.3 Async/await
- `async def`, `await`, `async for`, `async with` → not supported
- **PLR:** PLR §8.8 — Coroutines

### 5.4 Match statement (Python 3.10+)
- `match`/`case` → not supported
- **PLR:** PLR §8.6 — The match statement

### 5.5 Nonlocal statement
- `nonlocal x` → not supported (closures not modeled)
- **PLR:** PLR §7.13 — The nonlocal statement

### 5.6 Star expressions
- `*args`, `**kwargs`, `a, *b = [1,2,3]` → not supported
- **PLR:** PLR §6.2.4 — Starred assignment target
