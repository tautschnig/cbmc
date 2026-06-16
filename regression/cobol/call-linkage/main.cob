       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBPROG.
      * A called subprogram. Its PROCEDURE DIVISION USING formal LK-VAL is a
      * LINKAGE item bound (BY REFERENCE) to the caller's argument, so an
      * update is visible to the caller (IBM LR "CALL statement").
       DATA DIVISION.
       LINKAGE SECTION.
       01  LK-VAL  PIC 9(4).
       PROCEDURE DIVISION USING LK-VAL.
       MAIN-S.
           ADD 1 TO LK-VAL.
           GOBACK.
       END PROGRAM SUBPROG.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. MAINPGM.
      * The caller. Defined after SUBPROG, so SUBPROG's signature is known
      * and the CALL is linked: WS-N is copied into LK-VAL, SUBPROG runs, and
      * LK-VAL is copied back into WS-N (copy-in / call / copy-out).
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) VALUE 41.
       PROCEDURE DIVISION.
       MAIN-M.
           CALL "SUBPROG" USING WS-N.
           CALL "__CPROVER_assert" USING WS-N = 42.
           STOP RUN.
       END PROGRAM MAINPGM.
