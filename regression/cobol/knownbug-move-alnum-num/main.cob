       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-MOVE-ALNUM-NUM.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-A  PIC X(3) VALUE '123'.
       01  WS-N  PIC 9(3) VALUE 0.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "MOVE statement": moving an alphanumeric (display) source to
      *    a numeric receiver de-edits the digit characters, so WS-N = 123.
      *    The value model has no character content for the source, so the
      *    converted value is nondeterministic.
           MOVE WS-A TO WS-N.
           CALL "__CPROVER_assert" USING WS-N = 123.
           STOP RUN.
