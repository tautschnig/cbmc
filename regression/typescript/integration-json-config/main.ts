// Real-world harness: JSON path accessor (dot-notation).
// Exercises: object property access, conditionals, string comparison.
// Property: get(obj, "key") === obj.key for known paths.

interface Config {
    host: string;
    port: number;
    debug: boolean;
}

function getPort(config: Config): number {
    return config.port;
}

function getHost(config: Config): string {
    return config.host;
}

function isDebug(config: Config): boolean {
    return config.debug;
}

function main(): void {
    const config: Config = {
        host: "localhost",
        port: 8080,
        debug: true
    };

    // Property 1: Direct access matches getter functions.
    console.assert(getPort(config) === 8080);
    console.assert(getHost(config) === "localhost");
    console.assert(isDebug(config) === true);

    // Property 2: Computed access patterns.
    const port = config.port;
    console.assert(port > 0);
    console.assert(port < 65536);

    // Property 3: String field equality.
    console.assert(config.host.length === 9);
    console.assert(config.host.startsWith("local"));

    // Property 4: Boolean logic on config.
    if (config.debug) {
        console.assert(config.port === 8080);
    }
}
main();
