       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBBAD.
      * Out-of-range subscript: index 5 into an OCCURS 3 table is a
      * storage overrun (z/OS S0C4 with SSRANGE). cobol:subscript-range
      * must report the violation. IBM LR "Subscripting".
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-T.
           05  WS-E OCCURS 3 TIMES PIC 9(2).
       01  WS-I  PIC 9 VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 7 TO WS-E(WS-I).
           STOP RUN.
