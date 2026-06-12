       IDENTIFICATION DIVISION.
       PROGRAM-ID. CICS-STUB.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-RESP   PIC S9(8) COMP.
       01  WS-LEN    PIC S9(4) COMP.
       01  WS-DATA   PIC X(20).
       01  WS-X      PIC 9(4) COMP VALUE 5.
       PROCEDURE DIVISION.
       MAIN-PARA.
      *    An EIB field resolves and reads as a nondeterministic value.
           MOVE EIBCALEN TO WS-LEN.
      *    A stubbed command: its output operands become nondeterministic.
           EXEC CICS READ
               FILE('ACCTFILE')
               INTO(WS-DATA)
               RESP(WS-RESP)
           END-EXEC.
      *    Storage not named by the command keeps its value.
           CALL "__CPROVER_assert" USING WS-X = 5.
      *    CICS RETURN ends the transaction, so the next line is unreachable;
      *    if RETURN did not terminate, this false assertion would fail.
           EXEC CICS RETURN END-EXEC.
           CALL "__CPROVER_assert" USING WS-X = 99.
           STOP RUN.
