       IDENTIFICATION DIVISION.
       PROGRAM-ID. INITIALIZE-STMT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-GRP.
           05  WS-NUM   PIC 9(4) VALUE 99.
           05  WS-TXT   PIC X(3) VALUE 'ABC'.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    INITIALIZE sets numeric fields to ZERO and alphanumeric fields to
      *    SPACES (IBM LR "INITIALIZE statement").
           INITIALIZE WS-GRP.
           IF WS-NUM = 0 AND WS-TXT = SPACES
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
