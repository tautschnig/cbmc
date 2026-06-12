       IDENTIFICATION DIVISION.
       PROGRAM-ID. OCCURS-INDEXED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TBL.
           05  WS-ENT OCCURS 5 TIMES
                      ASCENDING KEY IS WS-VAL
                      INDEXED BY WS-IDX.
               10  WS-VAL  PIC 9(4).
       01  WS-R   PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    WS-IDX is an index-name from the INDEXED BY phrase: it is SET and
      *    used as a subscript (IBM LR "INDEXED BY phrase").
           SET WS-IDX TO 2.
           MOVE 7 TO WS-VAL(WS-IDX).
           MOVE WS-VAL(WS-IDX) TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 7.
           STOP RUN.
