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
  "semver:7.7.2:290a29b26644b16ad172c21797c5523788b537a7784ffd175607c4812653504e"
  "lodash.chunk:4.2.0:6aa88f76777f8e7962036c197845c4943e3fe1aaf7c5b7c2d4adc313c9009cf9"
  "is-number:7.0.0:7b75c1057198cf97696909a9bee176c9c5e9bcb5b03bf3ecef2f484defadd51e"
  "once:1.4.0:cf51460ba370c698f68b976e514d113497339ba018b6003e8e8eb569c6fccfcf"
  "array-unique:0.3.2:2fbdcf30f58eda555408afc8d61f763c988061e27f11589ac227661c1059792e"
  "is-plain-object:5.0.0:405ffdac90b988722906f2c25e627331c8ac952201c5e2eefab1c61f22d25bd3"
)
