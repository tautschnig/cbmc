       IDENTIFICATION DIVISION.
       PROGRAM-ID. C5END.
      * COMP-5 (NATIVE_BINARY) shares the big-endian two's-complement layout
      * of COMP (z/Architecture), so a byte view of value 1 is 0x00 0x01.
      * COMP-5 is not decimal-limited, but that does not affect this test.
      * IBM LR "USAGE clause" (COMP-5).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-G.
           05  WS-C  PIC 9(4) COMP-5 VALUE 1.
           05  WS-X  REDEFINES WS-C PIC X(2).
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           IF WS-X(1:1) = X'00' AND WS-X(2:1) = X'01'
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
