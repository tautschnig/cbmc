       IDENTIFICATION DIVISION.
       PROGRAM-ID. C2VAL.
      * A COMP-2 VALUE clause initialises the item to the literal's IEEE
      * double value (IBM LR "VALUE clause" with COMP-2).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-D  USAGE COMP-2 VALUE 2.5.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN.
           IF WS-D = 2.5
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
