       IDENTIFICATION DIVISION.
       PROGRAM-ID. EXIT-PERFORM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-I      PIC 9(3) VALUE 0.
       01  WS-COUNT  PIC 9(3) VALUE 0.
       01  WS-ODD    PIC 9(3) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    EXIT PERFORM leaves the inline loop early; EXIT PERFORM CYCLE skips
      *    the rest of the iteration (IBM LR "EXIT statement").
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 100
               IF WS-I > 5
                   EXIT PERFORM
               END-IF
      *        count only iterations 1..5; skip the even ones before counting
               IF WS-I = 2 OR WS-I = 4
                   EXIT PERFORM CYCLE
               END-IF
               ADD 1 TO WS-ODD
               ADD 1 TO WS-COUNT
           END-PERFORM.
      *    iterations 1,3,5 reach the ADDs (2 and 4 cycle out, 6 exits): 3.
           CALL "__CPROVER_assert" USING WS-COUNT = 3.
           CALL "__CPROVER_assert" USING WS-ODD = 3.
           CALL "__CPROVER_assert" USING WS-I = 6.
           STOP RUN.
