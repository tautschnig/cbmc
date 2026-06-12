       IDENTIFICATION DIVISION.
       PROGRAM-ID. EDITED-PIC.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    Numeric-edited items whose embedded '.' and ',' previously
      *    broke the scanner; they parse now (modelled as display fields).
       01  WS-AMT-DIS    PIC -ZZ,ZZ9.99.
       01  WS-COUNT-DIS  PIC ZZZ9.
       01  WS-BAL-DIS    PIC $$,$$9.99CR.
       01  WS-N          PIC 9(4) COMP VALUE 42.
       01  WS-R          PIC 9(4) COMP.
       PROCEDURE DIVISION.
       MAIN-PARA.
           ADD 8 TO WS-N GIVING WS-R.
           CALL "__CPROVER_assert" USING WS-R = 50.
           STOP RUN.
