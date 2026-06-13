       IDENTIFICATION DIVISION.
       PROGRAM-ID. OCCURS-DEPENDING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N    PIC 9(2) VALUE 3.
       01  WS-TBL.
      *    A variable-length table, modelled as fixed at its maximum (10).
           05  WS-ENT OCCURS 1 TO 10 TIMES DEPENDING ON WS-N
                      PIC 9(2).
       01  WS-R    PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 5 TO WS-ENT(2).
           MOVE 7 TO WS-ENT(3).
           ADD WS-ENT(2) WS-ENT(3) GIVING WS-R.
           CALL "__CPROVER_assert" USING WS-R = 12.
           STOP RUN.
