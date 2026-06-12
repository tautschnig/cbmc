       IDENTIFICATION DIVISION.
       PROGRAM-ID. ADD-OPERANDS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-X  PIC 9(4) COMP.
       01  WS-Y  PIC 9(4) COMP VALUE 10.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    A figurative constant and a literal as arithmetic operands.
           ADD 8 TO ZERO GIVING WS-X.
           CALL "__CPROVER_assert" USING WS-X = 8.
           ADD 5 TO WS-Y.
           CALL "__CPROVER_assert" USING WS-Y = 15.
      *    The RETURN-CODE special register.
           MOVE 16 TO RETURN-CODE.
           CALL "__CPROVER_assert" USING RETURN-CODE = 16.
           STOP RUN.
