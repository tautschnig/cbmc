       IDENTIFICATION DIVISION.
       PROGRAM-ID. CLSNUMRD.
      * IBM LR "Class condition": a zoned DISPLAY numeric item whose bytes
      * are aliased (here via REDEFINES) and loaded with a non-digit
      * character is NOT NUMERIC. The faithful zoned content makes this
      * exact: 'A' (0x41) in the last position fails the 0x30..0x39 test.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-G.
           05  WS-N  PIC 9(3).
           05  WS-A  REDEFINES WS-N PIC X(3).
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE "12A" TO WS-A.
           IF WS-N IS NUMERIC
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 0.
           STOP RUN.
