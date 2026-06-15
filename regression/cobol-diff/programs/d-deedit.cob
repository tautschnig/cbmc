       IDENTIFICATION DIVISION.
       PROGRAM-ID. D-DEEDIT.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-IN      PIC X(3) VALUE '042'.
       01  WS-N       PIC 9(3).
       01  WS-RESULT  PIC ZZ9.
       PROCEDURE DIVISION.
       MAIN-PARA.
           MOVE WS-IN TO WS-N.
           MOVE WS-N TO WS-RESULT.
           DISPLAY "[" WS-RESULT "]".
           STOP RUN.
