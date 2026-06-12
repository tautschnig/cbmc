       IDENTIFICATION DIVISION.
       PROGRAM-ID. OCCURS-GROUP.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TABLE.
           05  WS-ROW OCCURS 3 TIMES.
               10  WS-KEY    PIC 9(4) COMP.
               10  WS-VAL    PIC 9(4) COMP.
       01  WS-MATRIX.
           05  WS-MROW OCCURS 2 TIMES.
               10  WS-MCOL OCCURS 2 TIMES.
                   15  WS-CELL  PIC 9(4) COMP.
       01  WS-I    PIC 9(4) COMP.
       01  WS-SUM  PIC 9(8) COMP VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 3
               MOVE WS-I TO WS-KEY(WS-I)
               COMPUTE WS-VAL(WS-I) = WS-I * 10
           END-PERFORM.
           CALL "__CPROVER_assert" USING WS-KEY(2) = 2.
           CALL "__CPROVER_assert" USING WS-VAL(3) = 30.
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 3
               ADD WS-VAL(WS-I) TO WS-SUM
           END-PERFORM.
           CALL "__CPROVER_assert" USING WS-SUM = 60.
      *    Two-dimensional table: WS-CELL(row, col).
           MOVE 7 TO WS-CELL(2, 1).
           MOVE 9 TO WS-CELL(1, 2).
           CALL "__CPROVER_assert" USING WS-CELL(2, 1) = 7.
           CALL "__CPROVER_assert" USING WS-CELL(1, 2) = 9.
           STOP RUN.
