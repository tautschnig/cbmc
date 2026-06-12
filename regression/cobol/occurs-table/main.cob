       IDENTIFICATION DIVISION.
       PROGRAM-ID. OCCURS-TABLE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-TABLE.
           05  WS-ELEM   PIC 9(4) OCCURS 5 TIMES.
       01  WS-I          PIC 9(4).
       01  WS-SUM        PIC 9(6) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 5
               MOVE WS-I TO WS-ELEM(WS-I)
           END-PERFORM.
           PERFORM VARYING WS-I FROM 1 BY 1 UNTIL WS-I > 5
               ADD WS-ELEM(WS-I) TO WS-SUM
           END-PERFORM.
           CALL "__CPROVER_assert" USING WS-SUM = 15.
           CALL "__CPROVER_assert" USING WS-ELEM(3) = 3.
           STOP RUN.
