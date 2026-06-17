       IDENTIFICATION DIVISION.
       PROGRAM-ID. EBCZONE.
      * A zoned DISPLAY numeric stores its digits in EBCDIC (0xF0..0xF9),
      * not ASCII (IBM LR "USAGE DISPLAY", external decimal). Observed
      * through a REDEFINES alias, the value 123 is the bytes X'F1F2F3'.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(3) VALUE 123.
       01  WS-X  REDEFINES WS-N PIC X(3).
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           IF WS-X = X'F1F2F3'
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
