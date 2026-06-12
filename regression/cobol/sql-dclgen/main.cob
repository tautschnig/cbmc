       IDENTIFICATION DIVISION.
       PROGRAM-ID. SQL-DCLGEN.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    A DB2 DCLGEN member (MYDCL.dcl) is brought in via EXEC SQL INCLUDE.
      *    Its EXEC SQL DECLARE TABLE directive must be skipped (so CHAR(3) /
      *    DECIMAL(1) are not read as level numbers) and its host structure
      *    parsed.
           EXEC SQL INCLUDE MYDCL END-EXEC.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE 7 TO DCL-FOO.
           MOVE DCL-FOO TO WS-R.
           CALL "__CPROVER_assert" USING WS-R = 7.
           STOP RUN.
