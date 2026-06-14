       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-THRU-EXIT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-X  PIC 9 VALUE 0.
       01  WS-Y  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    PERFORM a paragraph range THRU its EXIT terminator; a GO TO the
      *    EXIT paragraph is the idiomatic early return from the range
      *    (IBM LR "PERFORM statement" / "GO TO statement"). Here the GO TO
      *    skips the second assignment, so WS-Y stays 0.
           PERFORM 100-WORK THRU 100-EXIT.
           CALL "__CPROVER_assert" USING WS-X = 1.
           CALL "__CPROVER_assert" USING WS-Y = 0.
           STOP RUN.
       100-WORK.
           MOVE 1 TO WS-X.
           GO TO 100-EXIT.
           MOVE 1 TO WS-Y.
       100-EXIT.
           EXIT.
