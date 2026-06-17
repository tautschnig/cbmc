       IDENTIFICATION DIVISION.
       PROGRAM-ID. SZONED.
      * Signed zoned DISPLAY with the default trailing overpunch sign (IBM
      * LR "USAGE DISPLAY"; "SIGN clause"): the sign rides in the zone nibble
      * of the last digit byte -- 0xC positive, 0xD negative -- over EBCDIC
      * digits. So -123 is the bytes X'F1F2D3' and +123 is X'F1F2C3', and the
      * value decodes back with its sign.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC S9(3) VALUE -123.
       01  WS-X  REDEFINES WS-N PIC X(3).
       01  WS-P  PIC S9(3) VALUE 123.
       01  WS-Y  REDEFINES WS-P PIC X(3).
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           IF WS-X = X'F1F2D3'
               ADD 1 TO WS-R
           END-IF.
           IF WS-Y = X'F1F2C3'
               ADD 1 TO WS-R
           END-IF.
           IF WS-N = -123 AND WS-P = 123
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
