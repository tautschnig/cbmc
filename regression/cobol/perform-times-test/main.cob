       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-TIMES-TEST.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N    PIC 9  VALUE 3.
       01  WS-CNT  PIC 99 VALUE 0.
       01  WS-DW   PIC 99 VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    PERFORM n TIMES with n an identifier (IBM LR "PERFORM statement").
           PERFORM WS-N TIMES
               ADD 1 TO WS-CNT
           END-PERFORM.
      *    WITH TEST AFTER is a do-while: the body runs once even though the
      *    UNTIL condition is already true (5 > 0), giving 15; with the default
      *    TEST BEFORE it would stay 5.
           PERFORM WITH TEST AFTER UNTIL WS-DW > 0
               ADD 10 TO WS-DW
           END-PERFORM.
           CALL "__CPROVER_assert" USING WS-CNT = 3.
           CALL "__CPROVER_assert" USING WS-DW = 15.
           STOP RUN.
