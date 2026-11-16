# Differential test of the model of Double.parseDouble and Float.parseFloat

`p/Main.java` compares `java.lang.CProverFloatParse` of the models with the
JDK's parseDouble and parseFloat on generated strings (exceptions and their
messages, the exact values, and that "any value" is only given where the JDK
returns one). The model is copied into package `p` with the CProver calls
replaced:

    M=../../../jbmc/lib/java-models-library/src/main/java/java/lang
    mkdir -p cls
    sed -e 's/^package java.lang;/package p;/' \
      -e 's/CProverString\.charAt(\([^,]*\), /\1.charAt(/g' \
      -e 's/import org.cprover.CProver;//; s/import org.cprover.CProverString;//' \
      -e 's/CProverRuntime.version()/p.Main.VERSION/g' \
      -e 's/CProver.nondetDouble()/Double.longBitsToDouble(0x7ff0000000000001L)/' \
      -e 's/CProver.nondetFloat()/Float.intBitsToFloat(0x7f800001)/' \
      -e 's/CProver.assume(.*);//' \
      -e 's/^final class CProverFloatParse/public final class CProverFloatParse/' \
      -e 's/    static double parseDouble/    public static double parseDouble/' \
      -e 's/    static float parseFloat/    public static float parseFloat/' \
      $M/CProverFloatParse.java > p/CProverFloatParse.java
    javac --release 8 -d cls p/*.java
    $JAVA_HOME/bin/java -Dv=VERSION -cp cls p.Main

On Java 8 to 25 (500,000 strings each) the only differences were "BADF"
lines up to Java 17 where the harness does not recognise the negative
"any value" sentinel (Math.abs of a NaN keeps its sign there).
