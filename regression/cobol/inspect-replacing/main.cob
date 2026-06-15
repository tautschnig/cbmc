       IDENTIFICATION DIVISION.
       PROGRAM-ID. INSPECT-REPLACING.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC X(7) VALUE 'A,B,C,D'.
       01  WS-B  PIC X(6) VALUE '  12  '.
       01  WS-C  PIC X(5) VALUE 'XAXAX'.
       01  WS-D  PIC X(4) VALUE 'WXYZ'.
       01  WS-R  PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "INSPECT statement", REPLACING: ALL / LEADING / FIRST /
      *    CHARACTERS replacement over the item's bytes.
           INSPECT WS-A REPLACING ALL ',' BY ';'.
           INSPECT WS-B REPLACING LEADING ' ' BY '0'.
           INSPECT WS-C REPLACING FIRST 'A' BY 'B'.
           INSPECT WS-D REPLACING CHARACTERS BY '*'.
           IF WS-A = 'A;B;C;D' AND WS-B = '0012  '
              AND WS-C = 'XBXAX' AND WS-D = '****'
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
