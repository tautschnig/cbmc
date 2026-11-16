#!/bin/bash
# Compares the models of java.math (jbmc/lib/java-models-library) with the
# java.math of a JDK on random values: the results and the exceptions (class
# and message) of each operation, skipping those that the model leaves to a
# nondeterministic choice (wide values).
#   run-difftest.sh JAVA_HOME [N]
# The models are copied as package "model", with CProverRuntime.mayRunOn
# answering for the running JDK and CProverString's functions as String's.
set -e
JH=${1:?JAVA_HOME of the JDK to compare with}
N=${2:-5000}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MODELS=$ROOT/jbmc/lib/java-models-library
API=$MODELS/target/cprover-api.jar
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src/model" "$WORK/cls"
cp "$HERE/difftest/model/Rt.java" "$WORK/src/model/"
for f in "$MODELS"/src/main/java/java/math/*.java; do
  sed -E "s/^package java.math;/package model;/; s/CProverRuntime\.mayRunOn/Rt.mayRunOn/g; s/CProverString\.charAt\(([^,]+), /\1.charAt(/g; s/CProverString\.substring\(([^,]+), /\1.substring(/g; s/java\.math\.(BigInteger|BigDecimal|RoundingMode|MathContext|CProverMath)/model.\1/g" "$f" > "$WORK/src/model/$(basename "$f")"
done
# (Java 8 has no BigInteger.sqrt)
if ! "$JH/bin/java" -version 2>&1 | grep -q '"1\.8'; then RELEASE="--release 9"; else
  sed -i 's/public BigInteger sqrt() {/public BigInteger sqrtModel() {/' "$WORK/src/model/BigInteger.java"
  sed '/check("sqrt"/d' "$HERE/difftest/DiffAll.java" > "$WORK/src/DiffAll.java"; fi
[ -f "$WORK/src/DiffAll.java" ] || cp "$HERE/difftest/DiffAll.java" "$WORK/src/"
cp "$HERE/difftest/DiffBD.java" "$HERE/difftest/DiffVO.java" "$WORK/src/"
"$JH/bin/javac" $RELEASE -nowarn -d "$WORK/cls" -cp "$API" "$WORK"/src/model/*.java "$WORK"/src/*.java 2>&1 | grep -v "^Note" || true
for t in DiffAll DiffBD; do "$JH/bin/java" -Xmx1g -cp "$WORK/cls:$API" $t "$N" | grep -E "^BAD|TOTAL"; done
"$JH/bin/java" -cp "$WORK/cls:$API" DiffVO
