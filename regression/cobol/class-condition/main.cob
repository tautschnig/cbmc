       IDENTIFICATION DIVISION.
       PROGRAM-ID. CLASS-CONDITION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-DIG  PIC X(3) VALUE '123'.
       01  WS-MIX  PIC X(3) VALUE '1A3'.
       01  WS-ALP  PIC X(3) VALUE 'ABC'.
       01  WS-R    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Class conditions on an alphanumeric item are exact over its bytes
      *    (IBM LR "Class condition"): '123' is NUMERIC, '1A3' is not, 'ABC'
      *    is ALPHABETIC.
           IF WS-DIG IS NUMERIC
              AND WS-MIX IS NOT NUMERIC
              AND WS-ALP IS ALPHABETIC
              AND WS-MIX IS NOT ALPHABETIC
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
