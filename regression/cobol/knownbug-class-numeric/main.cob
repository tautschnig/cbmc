       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-CLASS-NUMERIC.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(3) VALUE 123.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "Class condition": a numeric DISPLAY item holding valid
      *    digits IS NUMERIC. Class conditions on a numeric operand are
      *    nondeterministic (only alphanumeric operands are tested exactly).
           IF WS-N IS NUMERIC
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
