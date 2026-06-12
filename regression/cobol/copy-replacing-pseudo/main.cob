       IDENTIFICATION DIVISION.
       PROGRAM-ID. COPY-REPL-PSEUDO.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-REC.
      *    Pseudo-text replacement: (VAR) -> ACCT forms WS-ACCT-FLAG and the
      *    88-level WS-ACCT-ON from the template's WS-(VAR)-FLAG / WS-(VAR)-ON
      *    (IBM LR "COPY statement", REPLACING with pseudo-text).
           COPY MYTMPL REPLACING ==(VAR)== BY ==ACCT==.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF WS-ACCT-FLAG = 'Y'
               MOVE 1 TO WS-R
           END-IF.
           IF WS-ACCT-ON
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
