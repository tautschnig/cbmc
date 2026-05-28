// Real-world security analysis: @smithy/credential-provider-imds
// GREENGRASS_HOSTS allowlist bypass via prototype chain.
//
// Source: smithy-lang/smithy-typescript
//   packages/credential-provider-imds/src/fromContainerMetadata.ts
//
// The validator uses the `in` operator to check that
// AWS_CONTAINER_CREDENTIALS_FULL_URI's hostname is in an
// allowlist:
//
//   const GREENGRASS_HOSTS = { localhost: true, "127.0.0.1": true };
//   if (!parsed.hostname || !(parsed.hostname in GREENGRASS_HOSTS)) {
//       throw new CredentialsProviderError(...);
//   }
//
// `in` consults the prototype chain. Object.prototype has
// "constructor", "__proto__", "hasOwnProperty", "toString",
// "valueOf", "isPrototypeOf", "propertyIsEnumerable",
// "toLocaleString". Any URL whose hostname matches one of these
// passes the allowlist.
//
// The CBMC harness models the validator and uses
// __CPROVER_assert_in_allowlist (which checks string equality,
// not prototype-chain membership) to detect that "constructor"
// is not actually in the intended allowlist.

function harness(): void {
    // Attacker-controlled URL.
    const attackerHostname: string = "constructor";

    // The intended allowlist.
    const allowed: string[] = ["localhost", "127.0.0.1"];

    // The contract that the upstream code intends to enforce
    // (intersection of allowlist) but actually doesn't because
    // of the prototype-chain bug.
    __CPROVER_assert_in_allowlist(attackerHostname, allowed);
}
harness();
