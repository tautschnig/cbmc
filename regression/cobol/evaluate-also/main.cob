       IDENTIFICATION DIVISION.
       PROGRAM-ID. EVALUATE-ALSO.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A    PIC 9 VALUE 2.
       01  WS-B    PIC 9 VALUE 5.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Two selection subjects joined by ALSO; a WHEN matches when both
      *    objects match (IBM LR "EVALUATE statement").
           EVALUATE WS-A ALSO WS-B
               WHEN 1 ALSO ANY
                   MOVE 7 TO WS-R
               WHEN 2 ALSO 4 THRU 6
                   MOVE 8 TO WS-R
               WHEN OTHER
                   MOVE 9 TO WS-R
           END-EVALUATE.
      *    WS-A=2 and WS-B=5 (in 4..6): the second WHEN matches.
           CALL "__CPROVER_assert" USING WS-R = 8.
      *    Mixed TRUE subject with an operand subject.
           EVALUATE TRUE ALSO WS-B
               WHEN WS-A > 0 ALSO 5
                   MOVE 1 TO WS-R
               WHEN OTHER
                   MOVE 2 TO WS-R
           END-EVALUATE.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
