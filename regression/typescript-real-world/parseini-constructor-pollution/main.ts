// Real-world security analysis: an INI parser with an incomplete
// blocklist for dangerous section names.
//
// The vulnerable pattern (paraphrased):
//
//   const blockList = ["__proto__"];
//   if (blockList.includes(sectionName)) throw ...;
//   map[currentSection] = map[currentSection] || {};
//   map[currentSection][key] = value;
//
// When `currentSection === "constructor"`:
//   - `map["constructor"]` returns the inherited Object constructor
//   - `|| {}` does not fire (Object is truthy)
//   - `map["constructor"][key] = value` writes to the global Object
//
// Result: global Object pollution after parsing a single config
// file. Subsequent `Object.<key>` lookups in the same process
// return attacker-controlled values.
//
// __CPROVER_assert_safe_property_key flags the dangerous section
// name before it reaches the assignment.

function parseIniSection(sectionName: string): void {
    // The defensive contract: section names used as object keys
    // must not be __proto__, constructor, or prototype.
    __CPROVER_assert_safe_property_key(sectionName);
}

function harness(): void {
    // The vulnerable parser would accept this section name,
    // because the upstream blocklist only excludes __proto__ /
    // profile __proto__.
    const sectionName: string = "constructor";
    parseIniSection(sectionName);
}
harness();
