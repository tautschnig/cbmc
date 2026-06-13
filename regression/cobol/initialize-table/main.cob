       IDENTIFICATION DIVISION.
       PROGRAM-ID. INITIALIZE-TABLE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TBL.
           05  WS-E OCCURS 4 TIMES PIC 9(2).
       01  WS-SUM  PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 9 TO WS-E(1)
           MOVE 9 TO WS-E(2)
           MOVE 9 TO WS-E(3)
           MOVE 9 TO WS-E(4)
      *    INITIALIZE clears every occurrence of the table, not just the
      *    first (IBM LR "INITIALIZE statement").
           INITIALIZE WS-TBL.
           ADD WS-E(1) WS-E(2) WS-E(3) WS-E(4) GIVING WS-SUM.
           CALL "__CPROVER_assert" USING WS-SUM = 0.
           STOP RUN.
