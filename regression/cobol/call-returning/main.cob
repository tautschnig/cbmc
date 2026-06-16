       IDENTIFICATION DIVISION.
       PROGRAM-ID. ADDER.
      * The callee's PROCEDURE DIVISION RETURNING item is copied back to the
      * caller's RETURNING receiver on return (IBM LR "The PROCEDURE DIVISION
      * header"; "CALL statement" RETURNING). ADDER returns LK-A + 10.
       DATA DIVISION.
       LINKAGE SECTION.
       01  LK-A    PIC 9(4).
       01  LK-RES  PIC 9(4).
       PROCEDURE DIVISION USING LK-A RETURNING LK-RES.
       S.
           COMPUTE LK-RES = LK-A + 10.
           GOBACK.
       END PROGRAM ADDER.
       IDENTIFICATION DIVISION.
       PROGRAM-ID. MP.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(4) VALUE 5.
       01  WS-R  PIC 9(4) VALUE 0.
       PROCEDURE DIVISION.
       M.
           CALL "ADDER" USING WS-N RETURNING WS-R.
           CALL "__CPROVER_assert" USING WS-R = 15.
           STOP RUN.
       END PROGRAM MP.
