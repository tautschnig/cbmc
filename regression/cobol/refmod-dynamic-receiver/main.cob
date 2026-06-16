       IDENTIFICATION DIVISION.
       PROGRAM-ID. RMRECV.
      * IBM LR "Reference modification": a reference-modified receiver
      * Y(start:len) with a non-constant len is an alphanumeric receiver of
      * len characters at position start; a MOVE writes only that window,
      * leaving the rest of Y unchanged (I8). Here Y starts 'ABCDEF':
      *   MOVE "XXXXX" TO WS-Y(2:3) -> positions 2..4 become XXX -> "AXXXEF"
      *   MOVE WS-S    TO WS-Y(2:2) -> positions 2..3 become PQ  -> "APQDEF"
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-Y   PIC X(6).
       01  WS-S   PIC X(5) VALUE 'PQRST'.
       01  WS-N3  PIC 9 VALUE 3.
       01  WS-N2  PIC 9 VALUE 2.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 'ABCDEF' TO WS-Y.
           MOVE "XXXXX" TO WS-Y(2:WS-N3).
           IF WS-Y = "AXXXEF"
               ADD 1 TO WS-R
           END-IF.
           MOVE 'ABCDEF' TO WS-Y.
           MOVE WS-S TO WS-Y(2:WS-N2).
           IF WS-Y = "APQDEF"
               ADD 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 2.
           STOP RUN.
