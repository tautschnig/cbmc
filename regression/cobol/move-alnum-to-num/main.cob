       IDENTIFICATION DIVISION.
       PROGRAM-ID. MOVE-ALNUM-TO-NUM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC X(3) VALUE '123'.
       01  WS-N  PIC 9(3) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "MOVE statement": an alphanumeric source moved to a numeric
      *    receiver is de-edited (its digit characters form the value), so
      *    WS-N = 123.
           MOVE WS-A TO WS-N.
           CALL "__CPROVER_assert" USING WS-N = 123.
           STOP RUN.
