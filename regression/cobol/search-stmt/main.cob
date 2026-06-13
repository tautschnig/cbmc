       IDENTIFICATION DIVISION.
       PROGRAM-ID. SEARCH-STMT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TBL.
           05  WS-ENT OCCURS 5 TIMES
                      ASCENDING KEY IS WS-KEY
                      INDEXED BY WS-IX.
               10  WS-KEY   PIC 9(2).
               10  WS-DATA  PIC 9(2).
       01  WS-R    PIC 9(2) VALUE 0.
       01  WS-R2   PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 10 TO WS-KEY(1)   MOVE 1 TO WS-DATA(1)
           MOVE 20 TO WS-KEY(2)   MOVE 2 TO WS-DATA(2)
           MOVE 30 TO WS-KEY(3)   MOVE 3 TO WS-DATA(3)
           MOVE 40 TO WS-KEY(4)   MOVE 4 TO WS-DATA(4)
           MOVE 50 TO WS-KEY(5)   MOVE 5 TO WS-DATA(5)
      *    Serial SEARCH scans from the current index until a WHEN matches
      *    (IBM LR "SEARCH statement").
           SET WS-IX TO 1.
           SEARCH WS-ENT
               AT END MOVE 99 TO WS-R
               WHEN WS-KEY(WS-IX) = 30
                   MOVE WS-DATA(WS-IX) TO WS-R
           END-SEARCH.
           CALL "__CPROVER_assert" USING WS-R = 3.
      *    SEARCH ALL (binary search, modelled as a serial scan from the
      *    first element).
           SEARCH ALL WS-ENT
               AT END MOVE 88 TO WS-R2
               WHEN WS-KEY(WS-IX) = 30
                   MOVE WS-DATA(WS-IX) TO WS-R2
           END-SEARCH.
           CALL "__CPROVER_assert" USING WS-R2 = 3.
           STOP RUN.
