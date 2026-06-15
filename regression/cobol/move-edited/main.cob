       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-EDITED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-AMT  PIC 9(5)V99 VALUE 1234.5.
       01  WS-E1   PIC ZZ,ZZ9.99.
       01  WS-E2   PIC ZZ9.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "PICTURE clause" editing: a MOVE to a numeric-edited item
      *    applies zero suppression, grouping and the decimal point, so
      *    1234.50 -> ' 1,234.50' and 5 -> '  5'.
           MOVE WS-AMT TO WS-E1.
           CALL "__CPROVER_assert" USING WS-E1 = ' 1,234.50'.
           MOVE 5 TO WS-E2.
           CALL "__CPROVER_assert" USING WS-E2 = '  5'.
           STOP RUN.
