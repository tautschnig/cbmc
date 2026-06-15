       IDENTIFICATION DIVISION.
       PROGRAM-ID. COMPUTE-DIVISION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-T   PIC 9V99.
       01  WS-R   PIC 9V99.
       01  WS-H   PIC 99.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Division inside COMPUTE develops fractional digits like DIVIDE
      *    (IBM LR arithmetic / "DIVIDE statement"): without ROUNDED the
      *    quotient is truncated to the receiver scale, with ROUNDED it is
      *    rounded half away from zero.
           COMPUTE WS-T = 10 / 3.
           CALL "__CPROVER_assert" USING WS-T = 3.33.
           COMPUTE WS-R ROUNDED = 2 / 3.
           CALL "__CPROVER_assert" USING WS-R = 0.67.
           COMPUTE WS-H ROUNDED = 5 / 2.
           CALL "__CPROVER_assert" USING WS-H = 3.
           STOP RUN.
