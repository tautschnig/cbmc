// Real-world security analysis: decoder builds output from external
// input by computed-property assignment, without filtering keys.
// Pattern (paraphrased):
//
//   const out = {};
//   for (const k of decodedKeys) {
//     out[k] = decode(value);
//   }
//   return out;
//
// When `decodedKeys` includes "__proto__" (e.g. from a malicious
// network payload), `out["__proto__"] = X` invokes the prototype
// setter, replacing out's prototype with X. Downstream consumers
// then see fields injected via the prototype chain.
//
// __CPROVER_assert_safe_property_key flags the dangerous key
// before the assignment.

function decoded_map_handler(key: string): void {
    __CPROVER_assert_safe_property_key(key);
}

function harness(): void {
    // Attacker-controlled key from a decoded payload (CBOR map key,
    // JSON map key, XML map entry key, eventstream header name, or
    // URL query parameter name).
    const key: string = "__proto__";
    decoded_map_handler(key);
}
harness();
