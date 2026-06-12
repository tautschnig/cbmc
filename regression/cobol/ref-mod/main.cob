       IDENTIFICATION DIVISION.
       PROGRAM-ID. REF-MOD.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-DATE   PIC X(8) VALUE '20240115'.
       01  WS-YEAR   PIC X(4).
       01  WS-MM     PIC X(2).
       01  WS-TAIL   PIC X(6).
       01  WS-R      PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    Substring with explicit length: positions 1..4 and 5..6.
           MOVE WS-DATE(1:4) TO WS-YEAR.
           IF WS-YEAR = '2024'
               ADD 1 TO WS-R
           END-IF.
           MOVE WS-DATE(5:2) TO WS-MM.
           IF WS-MM = '01'
               ADD 1 TO WS-R
           END-IF.
      *    Substring with omitted length: from position 3 to the end.
           MOVE WS-DATE(3:) TO WS-TAIL.
           IF WS-TAIL = '240115'
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 3.
           STOP RUN.
