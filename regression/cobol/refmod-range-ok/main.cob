       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFOK.
      * In-range reference modification (IBM LR "Reference modification"):
      * WS-S(2:3) selects positions 2..4 of a 6-byte item, so
      * cobol:refmod-range holds.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S  PIC X(6) VALUE 'ABCDEF'.
       01  WS-D  PIC X(3).
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE WS-S(2:3) TO WS-D.
           STOP RUN.
