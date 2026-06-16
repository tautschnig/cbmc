       IDENTIFICATION DIVISION.
       PROGRAM-ID. MAINPGM.
      * Forward reference: the caller is defined BEFORE the callee. A first
      * signature-collection pass records every program's PROCEDURE DIVISION
      * USING/RETURNING layout, so this CALL links even though SUBPROG appears
      * later in the file (IBM LR "CALL statement"). WS-N 41 -> 42.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) VALUE 41.
       PROCEDURE DIVISION.
       MAIN-M.
           CALL "SUBPROG" USING WS-N.
           CALL "__CPROVER_assert" USING WS-N = 42.
           STOP RUN.
       END PROGRAM MAINPGM.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBPROG.
       DATA DIVISION.
       LINKAGE SECTION.
       01  LK-VAL  PIC 9(4).
       PROCEDURE DIVISION USING LK-VAL.
       MAIN-S.
           ADD 1 TO LK-VAL.
           GOBACK.
       END PROGRAM SUBPROG.
