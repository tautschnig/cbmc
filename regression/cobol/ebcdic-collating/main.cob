       IDENTIFICATION DIVISION.
       PROGRAM-ID. EBCCOLL.
      * Native (EBCDIC) collating sequence for alphanumeric comparison (IBM
      * LR "Comparison of two alphanumeric operands"; the native collating
      * sequence). In EBCDIC letters collate before digits and lowercase
      * before uppercase -- the opposite of ASCII -- so:
      *   "A" < "9"   (0xC1 < 0xF9)   -- false under ASCII
      *   "a" < "A"   (0x81 < 0xC1)   -- false under ASCII
      * Within a class the order is unchanged ("B" > "A").
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           IF "A" < "9"
               ADD 1 TO WS-R
           END-IF.
           IF "a" < "A"
               ADD 1 TO WS-R
           END-IF.
           IF "B" > "A"
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
