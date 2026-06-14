       IDENTIFICATION DIVISION.
       PROGRAM-ID. FILE-IO.
       DATA DIVISION.
       FILE SECTION.
       FD  MY-FILE.
       01  MY-REC.
           05  MY-KEY   PIC 9(2).
           05  MY-DATA  PIC X(3).
       WORKING-STORAGE SECTION.
       01  WS-EOF  PIC 9 VALUE 0.
       01  WS-W    PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN INPUT MY-FILE.
      *    READ delivers an unknown record or reaches end-of-file; both AT
      *    END phrases are reachable (IBM LR "READ statement").
           READ MY-FILE
               AT END MOVE 1 TO WS-EOF
               NOT AT END MOVE 2 TO WS-EOF
           END-READ.
           OPEN OUTPUT MY-FILE.
           MOVE 5 TO MY-KEY.
           WRITE MY-REC
               INVALID KEY MOVE 1 TO WS-W
               NOT INVALID KEY MOVE 2 TO WS-W
           END-WRITE.
           CLOSE MY-FILE.
           CALL "__CPROVER_assert" USING WS-EOF > 0.
           CALL "__CPROVER_assert" USING WS-W > 0.
           STOP RUN.
