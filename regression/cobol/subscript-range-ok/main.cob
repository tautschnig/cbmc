       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBOK.
      * In-range subscript (IBM LR "Subscripting"): index 3 into an
      * OCCURS 5 table is valid, so cobol:subscript-range holds.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-T.
           05  WS-E OCCURS 5 TIMES PIC 9(2).
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 7 TO WS-E(3).
           STOP RUN.
