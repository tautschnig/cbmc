       IDENTIFICATION DIVISION.
       PROGRAM-ID. SORT-STMT.
       DATA DIVISION.
       FILE SECTION.
       SD  SORT-FILE.
       01  SORT-REC.
           05  SORT-KEY  PIC 9(2).
       WORKING-STORAGE SECTION.
       01  WS-IN   PIC 9 VALUE 0.
       01  WS-OUT  PIC 9 VALUE 0.
       01  WS-EOF  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    SORT runs the INPUT and OUTPUT procedures; the sort itself is
      *    abstracted (IBM LR "SORT statement").
           SORT SORT-FILE ON ASCENDING KEY SORT-KEY
               INPUT PROCEDURE IS GET-RECS
               OUTPUT PROCEDURE IS PUT-RECS.
           CALL "__CPROVER_assert" USING WS-IN = 1.
           CALL "__CPROVER_assert" USING WS-OUT = 1.
           STOP RUN.
       GET-RECS.
           MOVE 1 TO WS-IN.
           MOVE 5 TO SORT-KEY.
           RELEASE SORT-REC.
       PUT-RECS.
           MOVE 1 TO WS-OUT.
           RETURN SORT-FILE
               AT END MOVE 1 TO WS-EOF
               NOT AT END CONTINUE
           END-RETURN.
