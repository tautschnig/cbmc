#!/usr/bin/env python3
"""Writes the models of StringBuilder and StringBuffer from one template:
the class javadoc and license of the old files are kept, the body is
replaced."""
import re
import sys

BASE = sys.argv[1]  # .../src/main/java/java/lang

BODY = r'''
    static final long serialVersionUID = SERIAL;

    // DIFFBLUE MODEL LIBRARY
    // The model follows AbstractStringBuilder of Java 8 to 25: its checks,
    // their exceptions and messages (CProverChecks), and "null" for null
    // arguments. The string solver does the rest: the methods marked
    // "Handled internally by CBMC" and the private cprover* methods are
    // replaced by JBMC (java_string_library_preprocess).

    /** Handled internally by CBMC: the empty sequence. */
    public CLASS() {
    }

    /** The empty sequence, after the check of the capacity. */
    public CLASS(int capacity) {
        this();
        if (capacity < 0) {
            // (Java allocates an array of the capacity)
            throw new NegativeArraySizeException(
                CProverChecks.negativeSizeMessage(capacity));
        }
    }

    /** Handled internally by CBMC: a copy of {@code str}. */
    public CLASS(String str) {
    }

    public CLASS(CharSequence seq) {
        this(seq.toString());
    }

    public SYNC int compareTo(CLASS another) {
        return toString().compareTo(another.toString());
    }

    /** Handled internally by CBMC. */
    public SYNC int length() {
        return CProver.nondetInt();
    }

    public SYNC int capacity() {
        // (any capacity that holds the sequence)
        int capacity = CProver.nondetInt();
        CProver.assume(capacity >= length());
        return capacity;
    }

    public SYNC void ensureCapacity(int minimumCapacity) {
    }

    public SYNC void trimToSize() {
    }

    public SYNC void setLength(int newLength) {
        if (newLength < 0) {
            throw new StringIndexOutOfBoundsException(newLength);
        }
        cproverSetLength(newLength);
    }

    public SYNC char charAt(int index) {
        if (index < 0 || index >= length()) {
            if (CProverChecks.java8()) {
                throw new StringIndexOutOfBoundsException(index);
            }
            CProverChecks.checkIndex(index, length());
        }
        return cproverCharAt(index);
    }

    public SYNC int codePointAt(int index) {
        if (index < 0 || index >= length()) {
            if (CProverChecks.java8()) {
                throw new StringIndexOutOfBoundsException(index);
            }
            CProverChecks.checkIndex(index, length());
        }
        return cproverCodePointAt(index);
    }

    public SYNC int codePointBefore(int index) {
        int i = index - 1;
        if (i < 0 || i >= length()) {
            if (CProverRuntime.version() <= 17) {
                throw new StringIndexOutOfBoundsException(index);
            }
            CProverChecks.checkIndex(i, length());
        }
        return cproverCodePointBefore(index);
    }

    public SYNC int codePointCount(int beginIndex, int endIndex) {
        if (beginIndex < 0 || endIndex > length() || beginIndex > endIndex) {
            if (CProverRuntime.version() <= 17) {
                throw new IndexOutOfBoundsException();
            }
            throw CProverChecks.index(CProverChecks.RANGE, beginIndex,
                endIndex, length());
        }
        return cproverCodePointCount(beginIndex, endIndex);
    }

    public SYNC int offsetByCodePoints(int index, int codePointOffset) {
        if (index < 0 || index > length()) {
            throw new IndexOutOfBoundsException();
        }
        return toString().offsetByCodePoints(index, codePointOffset);
    }

    public SYNC void getChars(int srcBegin, int srcEnd, char[] dst,
                              int dstBegin) {
        int count = length();
        if (CProverChecks.java8()) {
            if (srcBegin < 0) {
                throw new StringIndexOutOfBoundsException(srcBegin);
            }
            if ((srcEnd < 0) || (srcEnd > count)) {
                throw new StringIndexOutOfBoundsException(srcEnd);
            }
            if (srcBegin > srcEnd) {
                throw new StringIndexOutOfBoundsException(
                    "srcBegin > srcEnd");
            }
            // (System.arraycopy)
            int n = srcEnd - srcBegin;
            if (dstBegin < 0 || dstBegin > dst.length - n) {
                throw new ArrayIndexOutOfBoundsException();
            }
        } else {
            CProverChecks.checkRangeSIOOBE(srcBegin, srcEnd, count);
            int n = srcEnd - srcBegin;
            CProverChecks.checkRange(dstBegin, dstBegin + n, dst.length);
        }
        for (int i = srcBegin; i < srcEnd; i++) {
            dst[dstBegin + i - srcBegin] = cproverCharAt(i);
        }
    }

    public SYNC void setCharAt(int index, char ch) {
        if (index < 0 || index >= length()) {
            if (CProverChecks.java8()) {
                throw new StringIndexOutOfBoundsException(index);
            }
            CProverChecks.checkIndex(index, length());
        }
        cproverSetCharAt(index, ch);
    }

    public SYNC CLASS append(Object obj) {
        return append(String.valueOf(obj));
    }

    /** Handled internally by CBMC (a null str appends "null"). */
    public SYNC CLASS append(String str) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    /** Handled internally by CBMC (a null sb appends "null"). */
    public SYNC CLASS append(StringBuffer sb) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    public SYNC CLASS append(CharSequence s) {
        if (s == null) {
            return append("null");
        }
        return append(s.toString());
    }

    public SYNC CLASS append(CharSequence s, int start, int end) {
        // (a null s stands for "null", in a call of its own, so that s
        // keeps a single target where it is not null)
        if (s == null) {
            return append("null", start, end);
        }
        if (CProverChecks.java8()) {
            if ((start < 0) || (start > end) || (end > s.length())) {
                throw new IndexOutOfBoundsException(
                    "start " + start + ", end " + end + ", s.length() "
                    + s.length());
            }
        } else {
            CProverChecks.checkRange(start, end, s.length());
        }
        return append(CProverString.substring(s.toString(), start, end));
    }

    public SYNC CLASS append(char[] str) {
        return append(CProverString.ofCharArray(str, 0, str.length));
    }

    public SYNC CLASS append(char[] str, int offset, int len) {
        int end = offset + len;
        if (CProverChecks.java8()) {
            // (System.arraycopy)
            if (offset < 0 || len < 0 || offset > str.length - len) {
                throw new ArrayIndexOutOfBoundsException();
            }
        } else {
            CProverChecks.checkRange(offset, end, str.length);
        }
        return append(CProverString.ofCharArray(str, offset, len));
    }

    public SYNC CLASS append(boolean b) {
        if (b) {
            return append("true");
        }
        return append("false");
    }

    /** Handled internally by CBMC. */
    public SYNC CLASS append(char c) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    public SYNC CLASS append(int i) {
        return append(String.valueOf(i));
    }

    public SYNC CLASS appendCodePoint(int codePoint) {
        if (!Character.isValidCodePoint(codePoint)) {
            throw CProverChecks.invalidCodePoint(codePoint);
        }
        return cproverAppendCodePoint(codePoint);
    }

    public SYNC CLASS append(long lng) {
        return append(String.valueOf(lng));
    }

    public SYNC CLASS append(float f) {
        return append(String.valueOf(f));
    }

    public SYNC CLASS append(double d) {
        return append(String.valueOf(d));
    }

    public SYNC CLASS delete(int start, int end) {
        int count = length();
        if (CProverChecks.java8()) {
            if (start < 0) {
                throw new StringIndexOutOfBoundsException(start);
            }
            if (end > count) {
                end = count;
            }
            if (start > end) {
                throw new StringIndexOutOfBoundsException();
            }
        } else {
            if (end > count) {
                end = count;
            }
            CProverChecks.checkRangeSIOOBE(start, end, count);
        }
        return cproverDelete(start, end);
    }

    public SYNC CLASS deleteCharAt(int index) {
        if (index < 0 || index >= length()) {
            if (CProverChecks.java8()) {
                throw new StringIndexOutOfBoundsException(index);
            }
            CProverChecks.checkIndex(index, length());
        }
        return cproverDeleteCharAt(index);
    }

    public SYNC CLASS replace(int start, int end, String str) {
        int count = length();
        if (CProverChecks.java8()) {
            if (start < 0) {
                throw new StringIndexOutOfBoundsException(start);
            }
            if (start > count) {
                throw new StringIndexOutOfBoundsException("start > length()");
            }
            if (start > end) {
                throw new StringIndexOutOfBoundsException("start > end");
            }
            if (end > count) {
                end = count;
            }
        } else {
            if (end > count) {
                end = count;
            }
            CProverChecks.checkRangeSIOOBE(start, end, count);
        }
        // (a NullPointerException for a null str)
        int len = str.length();
        cproverDelete(start, end);
        return cproverInsert(start, str);
    }

    public SYNC String substring(int start) {
        return substring(start, length());
    }

    public SYNC CharSequence subSequence(int start, int end) {
        return substring(start, end);
    }

    public SYNC String substring(int start, int end) {
        int count = length();
        if (CProverChecks.java8()) {
            if (start < 0) {
                throw new StringIndexOutOfBoundsException(start);
            }
            if (end > count) {
                throw new StringIndexOutOfBoundsException(end);
            }
            if (start > end) {
                throw new StringIndexOutOfBoundsException(end - start);
            }
        } else {
            CProverChecks.checkRangeSIOOBE(start, end, count);
        }
        return cproverSubstring(start, end);
    }

    /** The check of an offset of an insert of {@code CharSequence}s. */
    private void checkInsertOffset(int offset) {
        if (offset < 0 || offset > length()) {
            if (CProverChecks.java8()) {
                throw new StringIndexOutOfBoundsException(offset);
            }
            CProverChecks.checkOffset(offset, length());
        }
    }

    public SYNC CLASS insert(int index, char[] str, int offset, int len) {
        if (CProverChecks.java8()) {
            if ((index < 0) || (index > length())) {
                throw new StringIndexOutOfBoundsException(index);
            }
            if ((offset < 0) || (len < 0) || (offset > str.length - len)) {
                throw new StringIndexOutOfBoundsException(
                    "offset " + offset + ", len " + len + ", str.length "
                    + str.length);
            }
        } else {
            checkInsertOffset(index);
            CProverChecks.checkRangeSIOOBE(offset, offset + len, str.length);
        }
        return cproverInsert(index, CProverString.ofCharArray(str, offset, len));
    }

    public SYNC CLASS insert(int offset, Object obj) {
        return insert(offset, String.valueOf(obj));
    }

    public SYNC CLASS insert(int offset, String str) {
        checkInsertOffset(offset);
        if (str == null) {
            return cproverInsert(offset, "null");
        }
        return cproverInsert(offset, str);
    }

    public SYNC CLASS insert(int offset, char[] str) {
        checkInsertOffset(offset);
        return cproverInsert(offset, CProverString.ofCharArray(str, 0, str.length));
    }

    public NSYNC CLASS insert(int dstOffset, CharSequence s) {
        if (s == null) {
            return insert(dstOffset, "null");
        }
        if (s instanceof String) {
            return insert(dstOffset, (String) s);
        }
        return insert(dstOffset, s, 0, s.length());
    }

    public SYNC CLASS insert(int dstOffset, CharSequence s, int start,
                              int end) {
        if (s == null) {
            return insert(dstOffset, "null", start, end);
        }
        if (CProverChecks.java8()) {
            if ((dstOffset < 0) || (dstOffset > length())) {
                throw new IndexOutOfBoundsException("dstOffset " + dstOffset);
            }
            if ((start < 0) || (end < 0) || (start > end)
                || (end > s.length())) {
                throw new IndexOutOfBoundsException(
                    "start " + start + ", end " + end + ", s.length() "
                    + s.length());
            }
        } else {
            CProverChecks.checkOffset(dstOffset, length());
            CProverChecks.checkRange(start, end, s.length());
        }
        return cproverInsert(
            dstOffset, CProverString.substring(s.toString(), start, end));
    }

    public NSYNC CLASS insert(int offset, boolean b) {
        return insert(offset, String.valueOf(b));
    }

    public SYNC CLASS insert(int offset, char c) {
        if (offset < 0 || offset > length()) {
            if (CProverChecks.java8()) {
                // (System.arraycopy)
                throw new ArrayIndexOutOfBoundsException();
            }
            CProverChecks.checkOffset(offset, length());
        }
        return cproverInsert(offset, String.valueOf(c));
    }

    public NSYNC CLASS insert(int offset, int i) {
        return insert(offset, String.valueOf(i));
    }

    public NSYNC CLASS insert(int offset, long l) {
        return insert(offset, String.valueOf(l));
    }

    public NSYNC CLASS insert(int offset, float f) {
        return insert(offset, String.valueOf(f));
    }

    public NSYNC CLASS insert(int offset, double d) {
        return insert(offset, String.valueOf(d));
    }

    public int indexOf(String str) {
        return indexOf(str, 0);
    }

    public SYNC int indexOf(String str, int fromIndex) {
        // (a NullPointerException for a null str)
        return toString().indexOf(str.toString(), fromIndex);
    }

    public int lastIndexOf(String str) {
        return lastIndexOf(str, length());
    }

    public SYNC int lastIndexOf(String str, int fromIndex) {
        return toString().lastIndexOf(str.toString(), fromIndex);
    }

    public SYNC CLASS reverse() {
        // (the surrogate pairs keep their order, as in Java)
        String s = toString();
        int size = s.length();
        cproverDelete(0, size);
        int i = size - 1;
        while (i >= 0) {
            char c = CProverString.charAt(s, i);
            if (Character.isLowSurrogate(c) && i > 0
                && Character.isHighSurrogate(CProverString.charAt(s, i - 1))) {
                append(CProverString.charAt(s, i - 1));
                append(c);
                i -= 2;
            } else {
                append(c);
                i -= 1;
            }
        }
        return this;
    }

    /** Handled internally by CBMC: a copy of the sequence. */
    public SYNC String toString() {
        return CProver.nondetWithoutNullForNotModelled();
    }

    public java.util.stream.IntStream chars() {
        return toString().chars();
    }

    public java.util.stream.IntStream codePoints() {
        return toString().codePoints();
    }

    // The operations of the string solver (handled internally by CBMC).

    private char cproverCharAt(int index) {
        return CProver.nondetChar();
    }

    private int cproverCodePointAt(int index) {
        return CProver.nondetInt();
    }

    private int cproverCodePointBefore(int index) {
        return CProver.nondetInt();
    }

    private int cproverCodePointCount(int beginIndex, int endIndex) {
        return CProver.nondetInt();
    }

    private String cproverSubstring(int start, int end) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    private CLASS cproverAppendCodePoint(int codePoint) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    private CLASS cproverInsert(int offset, String str) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    private CLASS cproverDelete(int start, int end) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    private CLASS cproverDeleteCharAt(int index) {
        return CProver.nondetWithoutNullForNotModelled();
    }

    private void cproverSetCharAt(int index, char ch) {
    }

    private void cproverSetLength(int newLength) {
    }
}
'''


def write(cls, serial, sync):
    path = f"{BASE}/{cls}.java"
    old = open(path).read()
    m = re.search(r"\n ?public final class " + cls + r"\b[^{]*\{", old)
    head = old[: m.end()]
    head = head.replace("import org.cprover.CProverString;",
                        "import org.cprover.CProverString;")
    body = (BODY.replace("CLASS", cls).replace("SERIAL", serial)
            .replace("NSYNC ", "").replace("SYNC ", sync))
    open(path, "w").write(head + "\n" + body.lstrip("\n"))


write("StringBuilder", "4383685877147921099L", "")
write("StringBuffer", "3388685877147921107L", "synchronized ")
