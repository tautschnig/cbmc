       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNSTRING-SPLIT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-SRC   PIC X(8) VALUE 'AB,CDE,F'.
       01  WS-1     PIC X(4) VALUE SPACES.
       01  WS-2     PIC X(4) VALUE SPACES.
       01  WS-C1    PIC 9.
       01  WS-D1    PIC X.
       01  WS-PTR   PIC 9(2) VALUE 1.
       01  WS-TAL   PIC 9(2) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "UNSTRING statement": split WS-SRC at ',' into WS-1, WS-2;
      *    WS-1='AB' (count 2, delimiter ','); after two fields the pointer
      *    sits at 8 (past 'AB,CDE,'), one more field 'F' remains, and two
      *    receivers were filled.
           UNSTRING WS-SRC DELIMITED BY ','
               INTO WS-1 DELIMITER IN WS-D1 COUNT IN WS-C1
                    WS-2
               WITH POINTER WS-PTR
               TALLYING IN WS-TAL.
           CALL "__CPROVER_assert" USING WS-1 = 'AB  '.
           CALL "__CPROVER_assert" USING WS-2 = 'CDE '.
           CALL "__CPROVER_assert" USING WS-C1 = 2.
           CALL "__CPROVER_assert" USING WS-D1 = ','.
           CALL "__CPROVER_assert" USING WS-PTR = 8.
           CALL "__CPROVER_assert" USING WS-TAL = 2.
           STOP RUN.
