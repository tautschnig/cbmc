       IDENTIFICATION DIVISION.
       PROGRAM-ID. INTRINSICS-MATH.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-NEG  PIC S9(4) VALUE -7.
       01  WS-B    PIC 9(4)  VALUE 3.
       01  WS-R    PIC 9     VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Numeric intrinsics computed exactly (IBM LR "Intrinsic functions").
           IF FUNCTION ABS(WS-NEG) = 7
              AND FUNCTION MAX(WS-B, 5, 2) = 5
              AND FUNCTION MIN(WS-B, 5, 2) = 2
              AND FUNCTION MOD(WS-NEG, 3) = 2
              AND FUNCTION REM(WS-NEG, 3) = -1
              AND FUNCTION INTEGER-PART(7) = 7
              AND FUNCTION SUM(WS-B, 5, 2) = 10
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
