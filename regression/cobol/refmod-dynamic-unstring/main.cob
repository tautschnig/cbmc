       IDENTIFICATION DIVISION.
       PROGRAM-ID. UNSDYN.
      * IBM LR "UNSTRING statement": the source is examined left to right to
      * its end. When the source is a reference modification with a
      * non-constant length, the scan stops at that run-time length. Here the
      * full field is 'AB,CD,X' but WS-A(1:5) limits the source to "AB,CD",
      * so splitting on ',' yields "AB" and "CD" and the trailing ",X" is
      * excluded (I8) -- the AWS CardDemo COPAUA0C idiom.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A   PIC X(7) VALUE 'AB,CD,X'.
       01  WS-L   PIC 9 VALUE 5.
       01  WS-P1  PIC X(3) VALUE SPACES.
       01  WS-P2  PIC X(3) VALUE SPACES.
       01  WS-R   PIC 9 VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
           UNSTRING WS-A(1:WS-L) DELIMITED BY ","
                    INTO WS-P1 WS-P2.
           IF WS-P1 = "AB " AND WS-P2 = "CD "
               MOVE 1 TO WS-R
           END-IF.
           CALL "__CPROVER_assert" USING WS-R = 1.
           STOP RUN.
