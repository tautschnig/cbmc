# Finding 2: @smithy/credential-provider-imds GREENGRASS_HOSTS allowlist bypass via prototype chain

**Project**: smithy-lang/smithy-typescript
  (`@smithy/credential-provider-imds`, used by AWS SDK JS v3)
**Severity**: High (SSRF / credential redirection in containerised
environments)
**Status**: New (not the same as open issue #1931)
**Found by**: CBMC TypeScript frontend +
`__CPROVER_assert_in_allowlist`

## Summary

`fromContainerMetadata.getCmdsUri` validates the
`AWS_CONTAINER_CREDENTIALS_FULL_URI` environment variable against
an allowlist of hosts:

```typescript
const GREENGRASS_HOSTS = {
  localhost: true,
  "127.0.0.1": true,
};

const parsed = parse(process.env[ENV_CMDS_FULL_URI]!);
if (!parsed.hostname || !(parsed.hostname in GREENGRASS_HOSTS)) {
  throw new CredentialsProviderError(...);
}
```

The `in` operator walks the prototype chain. `GREENGRASS_HOSTS`
inherits from `Object.prototype`, which has properties like
`constructor`, `__proto__`, `hasOwnProperty`, `toString`,
`valueOf`, `isPrototypeOf`, `propertyIsEnumerable`, `toLocaleString`.
Any URL whose hostname (after lowercasing) matches one of these
inherited names passes the allowlist check.

```
URL: http://constructor/path
  hostname: "constructor"
  in GREENGRASS_HOSTS: true   ← bypasses the allowlist
```

The companion `GREENGRASS_PROTOCOLS` check is also `in`-based, but
URL parsing rejects most non-`http:`/`https:` protocols, so it
mostly survives — though that's a defense-in-depth coincidence,
not a robustness property.

## Threat model

`AWS_CONTAINER_CREDENTIALS_FULL_URI` is set when running in
ECS / Fargate / similar container environments. An attacker who
can set or influence this environment variable (e.g. via a
malicious config file, build system that interpolates user input
into env, supply-chain compromise of a container image, or any
scenario where a less-privileged process can affect the SDK
caller's environment) can:

1. Set `AWS_CONTAINER_CREDENTIALS_FULL_URI=http://constructor/path`.
2. Bypass the GREENGRASS_HOSTS allowlist intended to restrict
   credential fetching to localhost.
3. Cause the SDK to send the IAM authorization token (if set) to
   the attacker-resolvable host, OR receive crafted credentials
   from it.

DNS resolution: "constructor", "__proto__", and similar are not
typically resolvable on the public internet, but in environments
with custom DNS, `/etc/hosts` entries, internal split-horizon
DNS, or development sandboxes, the attacker can ensure these
resolve to their endpoint. In particular, any attacker with root
access to the same host can edit `/etc/hosts`, but if they have
root they don't need this bug — the realistic exploit vectors
are:

- A multi-tenant or shared host where DNS is partly attacker-controlled.
- A container build pipeline where one stage sets
  `AWS_CONTAINER_CREDENTIALS_FULL_URI` and another stage runs the
  SDK with that env.
- Any future code path that takes `AWS_CONTAINER_CREDENTIALS_FULL_URI`
  from configuration files / metadata services that are partly
  untrusted.

The severity is **High** because it bypasses an explicit
security check in security-critical credential-fetching code,
and the same bug pattern is replicated in other parts of the
codebase that use `in` against allowlists.

## Source pointer

`packages/credential-provider-imds/src/fromContainerMetadata.ts`
lines 80-83 (current main branch as of 2026-05-28). The bug
predates and is unfixed by open issue #1931, which addresses
only the `url.parse` deprecation but proposes the same `in`
pattern in its fix.

## Suggested fix

Use `Object.prototype.hasOwnProperty.call`, `Object.hasOwn`, or
re-implement the allowlist as a Set:

```typescript
const GREENGRASS_HOSTS = new Set(["localhost", "127.0.0.1"]);
const GREENGRASS_PROTOCOLS = new Set(["http:", "https:"]);

const parsed = new URL(process.env[ENV_CMDS_FULL_URI]!);
if (!parsed.hostname || !GREENGRASS_HOSTS.has(parsed.hostname)) {
  throw new CredentialsProviderError(...);
}
if (!parsed.protocol || !GREENGRASS_PROTOCOLS.has(parsed.protocol)) {
  throw new CredentialsProviderError(...);
}
```

Set membership does not consult the prototype chain.

## Reproducer (Node.js):

```javascript
const { parse } = require("node:url");

const GREENGRASS_HOSTS = { localhost: true, "127.0.0.1": true };

const url = "http://constructor/path";
const parsed = parse(url);
console.log("hostname:", parsed.hostname);
console.log("in allowlist:", parsed.hostname in GREENGRASS_HOSTS);
// Output:
//   hostname: constructor
//   in allowlist: true   ← BUG
```

CBMC harness: `regression/typescript-real-world/smithy-imds-greengrass-bypass/main.ts`.
The harness models the validator and asserts membership via
`__CPROVER_assert_in_allowlist`, which uses string equality
rather than `in` and correctly rejects the prototype-chain
hosts.

## Disclosure

Not yet reported upstream. This is a credential-handling
component of the AWS SDK; recommend filing privately via the
project's security policy.
