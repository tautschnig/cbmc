       IDENTIFICATION DIVISION.
       PROGRAM-ID. SIZE-ERROR.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-C    PIC 99 VALUE 0.
       01  WS-D    PIC 99 VALUE 0.
       01  WS-R    PIC 9  VALUE 0.
       01  WS-R2   PIC 9  VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    50 + 60 = 110 does not fit PIC 99, so ON SIZE ERROR runs and WS-C
      *    is left unchanged (IBM LR "SIZE ERROR phrases").
           ADD 50 60 GIVING WS-C
               ON SIZE ERROR MOVE 1 TO WS-R
               NOT ON SIZE ERROR MOVE 2 TO WS-R
           END-ADD.
      *    10 + 20 = 30 fits, so NOT ON SIZE ERROR runs.
           ADD 10 20 GIVING WS-D
               ON SIZE ERROR MOVE 8 TO WS-R2
               NOT ON SIZE ERROR MOVE 9 TO WS-R2
           END-ADD.
           CALL "__CPROVER_assert" USING WS-R = 1.
           CALL "__CPROVER_assert" USING WS-R2 = 9.
           CALL "__CPROVER_assert" USING WS-D = 30.
           CALL "__CPROVER_assert" USING WS-C = 0.
           STOP RUN.
