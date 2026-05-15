// Real-world harness: URL parsing (simplified, single function).
// Exercises: indexOf, slice, parseInt, startsWith, conditionals.

function main(): void {
    const url = "https://example.com:443/api";

    // Extract protocol
    const protoEnd = url.indexOf("://");
    const protocol = url.slice(0, protoEnd);
    console.assert(protocol === "https");
    console.assert(protoEnd === 5);

    // Extract rest after "://"
    const afterProto = url.slice(protoEnd + 3);
    // afterProto = "example.com:443/api"

    // Extract path
    const pathStart = afterProto.indexOf("/");
    const path = afterProto.slice(pathStart);
    console.assert(path === "/api");

    // Extract host:port (before the path)
    const hostPort = afterProto.slice(0, pathStart);
    // hostPort = "example.com:443"

    // Extract port
    const colonPos = hostPort.indexOf(":");
    const portStr = hostPort.slice(colonPos + 1);
    const port = parseInt(portStr);
    console.assert(port === 443);

    // Extract host
    const host = hostPort.slice(0, colonPos);
    console.assert(host === "example.com");
}
main();
