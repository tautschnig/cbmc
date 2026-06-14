       IDENTIFICATION DIVISION.
       PROGRAM-ID. WRITE-FROM.
       DATA DIVISION.
       FILE SECTION.
       FD  MY-FILE.
       01  MY-REC   PIC X(3).
       WORKING-STORAGE SECTION.
       01  WS-SRC   PIC X(3) VALUE 'XYZ'.
       01  WS-R     PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           OPEN OUTPUT MY-FILE.
      *    WRITE record FROM id moves id into the record (IBM LR "WRITE
      *    statement", FROM phrase).
           WRITE MY-REC FROM WS-SRC
               INVALID KEY CONTINUE
               NOT INVALID KEY CONTINUE
           END-WRITE.
           IF MY-REC = 'XYZ'
               MOVE 1 TO WS-R
           END-IF.
           CLOSE MY-FILE.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
