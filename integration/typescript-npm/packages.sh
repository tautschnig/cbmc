# npm package metadata for TypeScript integration tests.
#
# Sourced by run_tests.sh. Each entry: package name, version, SHA256 hash.
# Versions here should match package.json (which Dependabot tracks).
#
# To update: bump version in package.json, re-download with:
#   curl -sL https://registry.npmjs.org/<pkg>/-/<pkg>-<version>.tgz | sha256sum
# then update the PACKAGES array below.
#
# Soundness: SHA256 hashes prevent tampering. We verify before extraction.

# Format: "name:version:sha256"
PACKAGES=(
  "ms:2.1.3:f6616e15e530ed552f9daa2d3ce71963947c6bc7c98c9b64fd3e673fd02622c6"
  "classnames:2.5.1:6c9e10fc21a14499b74bfe1e8e8dd7a6152da02fb575c3ffd3cd71f67390ebf1"
  "uuid:9.0.1:9fa6f415a7d9f7f8ea48e45beeb567686261da85cd395901c54c3515589e3f7e"
  "left-pad:1.3.0:870c0fe1096223a58d4f8832d08a7e651ea2fcadb8e6877b2fdc26b662d481dd"
)
