       IDENTIFICATION DIVISION.
       PROGRAM-ID. STRING-INTRINSICS-ITEM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC X(3) VALUE 'aBc'.
       01  WS-R  PIC X(3) VALUE SPACES.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "Intrinsic functions": UPPER-CASE/LOWER-CASE/REVERSE of an
      *    alphanumeric item are computed from its bytes.
           MOVE FUNCTION UPPER-CASE(WS-A) TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 'ABC'.
           MOVE FUNCTION REVERSE(WS-A) TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 'cBa'.
           STOP RUN.
