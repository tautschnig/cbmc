// =====================================================================
// Harness Template: Prototype-Key Injection (BUGGY form)
// =====================================================================
//
// **When to use**:
//   You suspect a key-injection sink is missing the
//   __CPROVER_assert_safe_property_key check. Pass a known-bad
//   key directly to demonstrate the assertion would have caught it.
//
// **What this verifies**:
//   __CPROVER_assert_safe_property_key correctly rejects
//   "__proto__" (a property of Object.prototype). CBMC reports
//   VERIFICATION FAILED.
//
// **Adapt to your audit**: replace "__proto__" with "constructor"
// or "prototype" to verify those are also caught.
//
// See also: `harness-template-prototype-key-injection-defensive/`
// for the form your fix should converge on.

function harness(): void {
    const key: string = "__proto__";
    __CPROVER_assert_safe_property_key(key);
}
harness();
