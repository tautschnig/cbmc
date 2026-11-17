# The index methods of String in each Java version

`P.java` prints the result or the exception (class and message) of
`charAt`, `codePointAt`, `codePointBefore`, `substring`, `subSequence`,
`codePointCount`, `offsetByCodePoints`, `getChars`, `getBytes`,
`contentEquals`, `regionMatches` and `String.valueOf(char[], int, int)` for
a Latin-1 and a UTF-16 string and indices in and out of range.

    javac --release 8 P.java
    for each JDK v: java -cp . P > v.txt   # JDK 9 and 10: -XX:+UseSerialGC
    python3 group.py                        # the results by version
    cp Test.java.in DIR/ && python3 gen_test.py DIR/Test.java

`gen_test.py` (run where the `{8..25}.txt` are) writes the test of
`jbmc/regression/jbmc-strings/StringIndexMessages`: one method per version
that asserts what that JDK gives. NullPointerException messages are not
compared (the JVM's helpful messages name the JDK's variables), and for
`charAt` on Java 9 to 17 both messages are allowed: Latin-1 strings give the
Java 8 message and UTF-16 ones that of `checkIndex`, and the JVM option
`-XX:-CompactStrings` makes every string UTF-16.
