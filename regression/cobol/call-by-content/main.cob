       IDENTIFICATION DIVISION.
       PROGRAM-ID. SUBP.
      * BY CONTENT passes a copy: the callee may modify its formal, but the
      * change is not propagated back to the caller (IBM LR "CALL statement",
      * BY CONTENT). Here SUBP adds 100 to its formal, yet the caller's WS-N
      * is unchanged.
       DATA DIVISION.
       LINKAGE SECTION.
       01  LK-V  PIC 9(4).
       PROCEDURE DIVISION USING LK-V.
       S.
           ADD 100 TO LK-V.
           GOBACK.
       END PROGRAM SUBP.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. MP.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) VALUE 41.
       PROCEDURE DIVISION.
       M.
           CALL "SUBP" USING BY CONTENT WS-N.
           CALL "__CPROVER_assert" USING WS-N = 41.
           STOP RUN.
       END PROGRAM MP.
