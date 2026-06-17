       IDENTIFICATION DIVISION.
       PROGRAM-ID. HEX-LITERAL.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    A hexadecimal-alphanumeric literal is raw bytes; a character
      *    literal is EBCDIC on z/OS (IBM LR "Hexadecimal-alphanumeric
      *    literals"; "Character set"). So "AB" is X'C1C2' (EBCDIC), and the
      *    ASCII bytes X'4142' are NOT "AB".
       01  WS-E   PIC X(2) VALUE X'C1C2'.
       01  WS-A   PIC X(2) VALUE X'4142'.
       01  WS-R   PIC 9    VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-E = "AB"
               ADD 1 TO WS-R
           END-IF.
           IF WS-A NOT = "AB"
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
