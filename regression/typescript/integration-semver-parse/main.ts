// Real-world verification harness: semver parsing and comparison.
// Simplified version that exercises string operations within the
// solver's capacity.

function parseMajor(version: string): number {
    const dot = version.indexOf(".");
    if (dot < 0) return 0;
    const majorStr = version.slice(0, dot);
    return parseInt(majorStr);
}

function main(): void {
    // Property 1: parseMajor extracts the correct major version.
    console.assert(parseMajor("1.2.3") === 1);
    console.assert(parseMajor("2.0.0") === 2);
    console.assert(parseMajor("10.5.3") === 10);

    // Property 2: Major version comparison.
    const m1 = parseMajor("2.0.0");
    const m2 = parseMajor("1.0.0");
    console.assert(m1 > m2);

    // Property 3: Same major → equal.
    const m3 = parseMajor("1.5.0");
    const m4 = parseMajor("1.9.9");
    console.assert(m3 === m4);

    // Property 4: Direct string operations.
    const v = "3.14.159";
    const dot = v.indexOf(".");
    console.assert(dot === 1);
    const rest = v.slice(dot + 1);
    console.assert(rest.length === 6);  // "14.159"
    console.assert(rest.startsWith("14"));
}
main();
