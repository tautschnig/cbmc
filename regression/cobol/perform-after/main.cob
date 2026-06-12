       IDENTIFICATION DIVISION.
       PROGRAM-ID. PERFORM-AFTER.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-MATRIX.
           05  WS-ROW OCCURS 3 TIMES.
               10  WS-CELL OCCURS 3 TIMES PIC 9(2).
       01  WS-I    PIC 9 VALUE 0.
       01  WS-J    PIC 9 VALUE 0.
       01  WS-SUM  PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Nested iteration: WS-J is re-initialised for each WS-I (IBM LR
      *    "PERFORM statement", VARYING ... AFTER).
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 3
                   AFTER WS-J FROM 1 BY 1 UNTIL WS-J > 3
               MOVE 2 TO WS-CELL(WS-I, WS-J)
           END-PERFORM.
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 3
                   AFTER WS-J FROM 1 BY 1 UNTIL WS-J > 3
               ADD WS-CELL(WS-I, WS-J) TO WS-SUM
           END-PERFORM.
      *    9 cells, each 2, sum to 18.
           CALL "__CPROVER_assert" USING WS-SUM = 18.
           STOP RUN.
