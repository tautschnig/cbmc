       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-INTRINSICS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    UPPER-CASE/LOWER-CASE/REVERSE of a string literal are computed at
      *    compile time (IBM LR "Intrinsic functions").
           IF FUNCTION UPPER-CASE('abc') = 'ABC'
              AND FUNCTION LOWER-CASE('XYZ') = 'xyz'
              AND FUNCTION REVERSE('abc') = 'cba'
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
