       IDENTIFICATION DIVISION.
       PROGRAM-ID. NUMVAL-FN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    NUMVAL / NUMVAL-C of a string literal are converted at compile time
      *    (IBM LR "NUMVAL"/"NUMVAL-C function").
           IF FUNCTION NUMVAL('123.45') = 123.45
              AND FUNCTION NUMVAL(' -7 ') = -7
              AND FUNCTION NUMVAL-C('$1,234.50') = 1234.50
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
