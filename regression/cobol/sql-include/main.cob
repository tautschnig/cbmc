       IDENTIFICATION DIVISION.
       PROGRAM-ID. SQL-INCLUDE.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
      *    The Db2 precompiler's INCLUDE copies a member, like COPY. MYSQLREC
      *    is on the search path (Db2 SQL Reference, "INCLUDE").
           EXEC SQL INCLUDE MYSQLREC END-EXEC.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           IF MY-STATUS-OK
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
