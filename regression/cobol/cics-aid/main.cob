       IDENTIFICATION DIVISION.
       PROGRAM-ID. CICS-AID.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    DFHAID is not on the search path; it is supplied by the bundled
      *    copybook library (cobol_copybooks.h) and expanded here.
           COPY DFHAID.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    DFHENTER (from the bundled DFHAID) and EIBAID (from the EIB) both
      *    resolve; EIBAID is nondeterministic so both branches are reachable
      *    and the sound bound below holds.
           IF EIBAID = DFHENTER
               MOVE 1 TO WS-R
           ELSE
               MOVE 2 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R > 0.
           STOP RUN.
