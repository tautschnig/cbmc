       IDENTIFICATION DIVISION.
       PROGRAM-ID. REFBAD.
      * Out-of-range reference modification: WS-S(5:4) would read
      * positions 5..8 of a 6-byte item. cobol:refmod-range must report
      * the violation. IBM LR "Reference modification".
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-S  PIC X(6) VALUE 'ABCDEF'.
       01  WS-D  PIC X(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE WS-S(5:4) TO WS-D.
           STOP RUN.
