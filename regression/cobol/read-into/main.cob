       IDENTIFICATION DIVISION.
       PROGRAM-ID. READ-INTO.
       DATA DIVISION.
       FILE SECTION.
       FD  MY-FILE.
       01  MY-REC   PIC X(5).
       WORKING-STORAGE SECTION.
       01  WS-COPY  PIC X(5).
       01  WS-DATE  PIC 9(6) VALUE 0.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN INPUT MY-FILE.
      *    READ ... INTO copies the record, so WS-COPY equals MY-REC even
      *    though both are unknown (IBM LR "READ statement").
           READ MY-FILE INTO WS-COPY
               AT END CONTINUE
               NOT AT END CONTINUE
           END-READ.
           IF MY-REC = WS-COPY
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
      *    ACCEPT FROM DATE yields a run-time value (havoced); just check it
      *    lowers and the program completes.
           ACCEPT WS-DATE FROM DATE.
           CLOSE MY-FILE.
           STOP RUN.
