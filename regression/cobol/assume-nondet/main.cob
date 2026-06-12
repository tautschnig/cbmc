       IDENTIFICATION DIVISION.
       PROGRAM-ID. ASSUME-NONDET.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-X    PIC 9(4).
       PROCEDURE DIVISION.
       MAIN-PARA.
           CALL "__CPROVER_assume" USING WS-X > 10.
           CALL "__CPROVER_assert" USING WS-X > 5.
           STOP RUN.
