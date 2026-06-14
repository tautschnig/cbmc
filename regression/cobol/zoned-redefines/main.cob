       IDENTIFICATION DIVISION.
       PROGRAM-ID. ZONED-REDEFINES.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-GRP.
           05  WS-NUM  PIC 9(3) VALUE 100.
           05  WS-RED  REDEFINES WS-NUM PIC X(3).
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    An unsigned DISPLAY numeric aliased by an alphanumeric REDEFINES is
      *    stored as zoned decimal (ASCII digits), so its bytes read as the
      *    digit string (IBM LR "USAGE DISPLAY"). The VALUE, a runtime write,
      *    and the numeric value must all be consistent.
           CALL "__CPROVER_assert" USING WS-RED = '100'.
           ADD 23 TO WS-NUM.
           CALL "__CPROVER_assert" USING WS-RED = '123'.
           CALL "__CPROVER_assert" USING WS-NUM = 123.
           STOP RUN.
