       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-COMP2-FLOAT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-F  USAGE COMP-2.
       01  WS-G  USAGE COMP-2.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "USAGE clause" (COMP-2): a long floating-point item. 3 / 2
      *    is 1.5. The value model has no float type (COMP-1/2 are modelled in
      *    the integer value domain), so this does not hold.
           MOVE 3 TO WS-F.
           DIVIDE WS-F BY 2 GIVING WS-G.
           CALL "__CPROVER_assert" USING WS-G = 1.5.
           STOP RUN.
