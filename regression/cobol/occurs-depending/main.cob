       IDENTIFICATION DIVISION.
       PROGRAM-ID. ODOSUB.
      * OCCURS DEPENDING ON (IBM LR "OCCURS clause" format 2): a subscript
      * must not exceed the current number of occurrences, i.e. the run-time
      * value of the DEPENDING ON item -- not the static maximum. Here the
      * current length is 3, so WS-E(3) is valid but WS-E(4) is out of range
      * even though 4 <= the maximum of 5 (I7).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9 VALUE 3.
       01  WS-T.
           05  WS-E OCCURS 1 TO 5 TIMES DEPENDING ON WS-N PIC 9(2).
       PROCEDURE DIVISION.
       MAIN.
           MOVE 7 TO WS-E(3).
           MOVE 8 TO WS-E(4).
           STOP RUN.
