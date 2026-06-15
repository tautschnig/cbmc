       IDENTIFICATION DIVISION.
       PROGRAM-ID. KB-MOVE-EDITED.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-N  PIC 9(3) VALUE 123.
       01  WS-E  PIC ZZ9.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    IBM LR "MOVE statement" / "PICTURE clause" editing: moving 123 to a
      *    PIC ZZ9 edited item produces the characters '123' (no leading zeros
      *    to suppress here). The frontend does not apply numeric editing.
           MOVE WS-N TO WS-E.
           CALL "__CPROVER_assert" USING WS-E = '123'.
           STOP RUN.
