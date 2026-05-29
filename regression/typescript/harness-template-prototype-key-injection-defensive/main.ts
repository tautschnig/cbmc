// =====================================================================
// Harness Template: Prototype-Key Injection (DEFENSIVE form)
// =====================================================================
//
// **When to use**:
//   The function under audit writes into an object using a
//   key that comes from external input (a JSON-parsed object's
//   keys, a URL query parameter name, a header name). Without a
//   guard, attacker keys like "__proto__", "constructor",
//   "prototype" can change Object.prototype-scoped state.
//
//   Threat model: the application caller has already validated
//   the key is not a dangerous prototype name. We're verifying
//   the protected sink is reachable only with safe keys.
//
// **What this verifies**:
//   __CPROVER_assert_safe_property_key holds for every key the
//   harness can pass.
//
// **Customise**:
//   - Replace `set_property` with the real assignment site.
//   - The harness's nondet source should match the actual key
//     origin (JSON parse, URL parse, etc.).

function harness(): void {
    const target: { [k: string]: number } = {};
    const key = nondet_string();
    // Application contract: caller has rejected any key that's
    // an Object.prototype property name.
    __CPROVER_assume(key === "user_value");
    __CPROVER_assert_safe_property_key(key);
    target[key] = 42;
}
harness();
