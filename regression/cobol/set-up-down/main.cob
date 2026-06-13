       IDENTIFICATION DIVISION.
       PROGRAM-ID. SET-UP-DOWN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TBL.
           05  WS-E OCCURS 5 TIMES PIC 9(2) INDEXED BY WS-IX.
       01  WS-R    PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           SET WS-IX TO 1.
           SET WS-IX UP BY 2.
           MOVE 7 TO WS-E(WS-IX).
           SET WS-IX DOWN BY 1.
           MOVE 4 TO WS-E(WS-IX).
      *    WS-E(3) = 7 and WS-E(2) = 4 after SET UP/DOWN BY (IBM LR "SET
      *    statement", format 4).
           ADD WS-E(2) WS-E(3) GIVING WS-R.
           CALL "__CPROVER_assert" USING WS-R = 11.
           STOP RUN.
