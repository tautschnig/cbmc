       IDENTIFICATION DIVISION.
       PROGRAM-ID. INTRINSICS.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-NAME  PIC X(20).
       01  WS-LEN   PIC 9(4) COMP.
       01  WS-RC    PIC S9(8) COMP.
       01  WS-N     PIC 9(8) COMP.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    LENGTH OF special register and FUNCTION LENGTH are exact.
           MOVE LENGTH OF WS-NAME TO WS-LEN.
           CALL "__CPROVER_assert" USING WS-LEN = 20.
           COMPUTE WS-LEN = FUNCTION LENGTH(WS-NAME).
           CALL "__CPROVER_assert" USING WS-LEN = 20.
      *    DFHRESP(NORMAL) = 0; the RESP field is nondeterministic.
           IF WS-RC = DFHRESP(NORMAL)
               MOVE 1 TO WS-R
           ELSE
               MOVE 2 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R > 0.
      *    NUMVAL is nondeterministic; just exercise that it parses. WS-LEN is
      *    unaffected by the COMPUTE into WS-N.
           COMPUTE WS-N = FUNCTION NUMVAL(WS-NAME).
           CALL "__CPROVER_assert" USING WS-LEN = 20.
           STOP RUN.
