# The models of StringBuilder and StringBuffer

`gen_sb.py DIR` rewrites `DIR/StringBuilder.java` and `DIR/StringBuffer.java`
(the `java/lang` directory of `jbmc/lib/java-models-library`) from one
template: the checks of AbstractStringBuilder of Java 8 to 25 with their
exceptions and messages (`CProverChecks`), then the private `cprover*`
methods that JBMC replaces by the string solver. The class javadoc and
licence of the files are kept. Edit the template here and rerun it rather
than editing the two models.

`extract.py` lists the methods of AbstractStringBuilder, StringBuilder and
StringBuffer of JDK sources (used to check that the models cover each
version's API).
