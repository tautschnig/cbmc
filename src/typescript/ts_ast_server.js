// TypeScript AST server for CBMC's TypeScript frontend.
//
// Listens on a Unix domain socket (path in argv[2]). For each
// connection:
//   - Read one newline-terminated UTF-8 request from the socket.
//     Format: "<source-path>\n".  The server parses as soon as a
//     newline is seen, so the client doesn't have to half-close.
//   - Parse + type-check using TypeScript's Language Service (shared
//     across connections: lib.d.ts is loaded once, subsequent parses
//     reuse the cached types).
//   - Emit a netstring-framed JSON AST: "<length>\n<json-bytes>\n"
//     where length is an ASCII integer (byte length of the JSON).
//     Framing avoids ambiguity when the client reads until close.
//   - Close the connection.
//
// Lifecycle: parent process (cbmc_ts_server wrapper) signals exit
// with SIGTERM or by closing stdin.
//
// The server is paired with client logic in
// src/typescript/typescript_language.cpp which discovers the socket
// via the CBMC_TS_SERVER_SOCKET env variable.

const net = require("net");
const fs = require("fs");
const ts = require("/usr/local/lib/node_modules/typescript");

const socketPath = process.argv[2];
if (!socketPath) {
  process.stderr.write("usage: node ts_ast_server.js <socket-path>\n");
  process.exit(2);
}

// ---- Language Service setup ----
// A single Language Service is shared across all connections. When a
// request arrives, the source file is registered in the `files` map
// and the LS's getProgram() re-validates. The LS caches lib.d.ts and
// type info across calls.
let files = {};
let version = 0;

const compilerOptions = {
  target: ts.ScriptTarget.ES2022,
  module: ts.ModuleKind.CommonJS,
  strict: true,
  noEmit: true,
  skipLibCheck: true,
  jsx: ts.JsxEmit.React,
};

const host = {
  getScriptFileNames: () => Object.keys(files),
  getScriptVersion: (f) => (files[f] ? files[f].version.toString() : "0"),
  getScriptSnapshot: (f) => {
    if (files[f]) return ts.ScriptSnapshot.fromString(files[f].content);
    if (fs.existsSync(f))
      return ts.ScriptSnapshot.fromString(fs.readFileSync(f, "utf-8"));
    return undefined;
  },
  getCurrentDirectory: () => process.cwd(),
  getCompilationSettings: () => compilerOptions,
  getDefaultLibFileName: (opts) => ts.getDefaultLibFilePath(opts),
  fileExists: ts.sys.fileExists,
  readFile: ts.sys.readFile,
  readDirectory: ts.sys.readDirectory,
  directoryExists: ts.sys.directoryExists,
  getDirectories: ts.sys.getDirectories,
};

const ls = ts.createLanguageService(host);

// ---- AST walker (mirrors the inline script in typescript_language.cpp) ----
function makeN2j(sourceFile, checker) {
  function n2j(node) {
    const r = {
      _kind: ts.SyntaxKind[node.kind],
      _pos: {
        line:
          sourceFile.getLineAndCharacterOfPosition(node.getStart()).line + 1,
        col: sourceFile.getLineAndCharacterOfPosition(node.getStart())
          .character,
      },
    };
    try {
      r._type = checker.typeToString(checker.getTypeAtLocation(node));
    } catch (e) {}
    switch (node.kind) {
      case ts.SyntaxKind.SourceFile:
        r.statements = node.statements.map(n2j);
        break;
      case ts.SyntaxKind.VariableStatement:
        r.declarationList = n2j(node.declarationList);
        break;
      case ts.SyntaxKind.VariableDeclarationList:
        r.declarations = node.declarations.map(n2j);
        r.flags =
          node.flags & ts.NodeFlags.Let
            ? "let"
            : node.flags & ts.NodeFlags.Const
            ? "const"
            : "var";
        break;
      case ts.SyntaxKind.VariableDeclaration:
        r.name = n2j(node.name);
        if (node.type) r.typeAnnotation = n2j(node.type);
        if (node.initializer) r.initializer = n2j(node.initializer);
        break;
      case ts.SyntaxKind.Identifier:
        r.text = node.text;
        break;
      case ts.SyntaxKind.NumericLiteral:
        r.text = node.text;
        r.value = parseFloat(node.text);
        break;
      case ts.SyntaxKind.BigIntLiteral:
        r.text = node.text;
        break;
      case ts.SyntaxKind.RegularExpressionLiteral:
        r.text = node.text;
        break;
      case ts.SyntaxKind.YieldExpression:
        if(node.expression) r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.StringLiteral:
      case ts.SyntaxKind.NoSubstitutionTemplateLiteral:
      case ts.SyntaxKind.FirstTemplateToken:
        r.text = node.text;
        break;
      case ts.SyntaxKind.TrueKeyword:
        r.value = true;
        break;
      case ts.SyntaxKind.FalseKeyword:
        r.value = false;
        break;
      case ts.SyntaxKind.NullKeyword:
        r.value = null;
        break;
      case ts.SyntaxKind.FunctionDeclaration:
      case ts.SyntaxKind.ArrowFunction:
      case ts.SyntaxKind.FunctionExpression:
        if (node.name) r.name = n2j(node.name);
        if (node.asteriskToken) r.isGenerator = true;
        r.parameters = node.parameters.map(n2j);
        if (node.type) r.returnType = n2j(node.type);
        if (node.typeParameters)
          r.typeParameters = node.typeParameters.map(n2j);
        if (node.body) r.body = n2j(node.body);
        try {
          const sig = checker.getSignatureFromDeclaration(node);
          if (sig) {
            r._returnType = checker.typeToString(sig.getReturnType());
            r._paramTypes = sig
              .getParameters()
              .map((p) =>
                checker.typeToString(
                  checker.getTypeOfSymbolAtLocation(p, node)
                )
              );
          }
        } catch (e) {}
        break;
      case ts.SyntaxKind.Parameter:
        r.name = n2j(node.name);
        if (node.type) r.typeAnnotation = n2j(node.type);
        if (node.initializer) r.initializer = n2j(node.initializer);
        if (node.dotDotDotToken) r.isRest = true;
        // Parameter-property modifiers (public/private/protected/
        // readonly) turn a constructor parameter into a class field.
        // We emit the list of modifier kinds so the converter can
        // recognise them.
        if (node.modifiers && node.modifiers.length > 0) {
          r.modifiers = node.modifiers.map((m) => ({
            _kind: ts.SyntaxKind[m.kind],
          }));
        }
        break;
      case ts.SyntaxKind.Block:
        r.statements = node.statements.map(n2j);
        break;
      case ts.SyntaxKind.ReturnStatement:
        if (node.expression) r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.ExpressionStatement:
        r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.BinaryExpression:
        r.left = n2j(node.left);
        r.operator = ts.SyntaxKind[node.operatorToken.kind];
        r.right = n2j(node.right);
        break;
      case ts.SyntaxKind.PrefixUnaryExpression:
        r.operator = ts.SyntaxKind[node.operator];
        r.operand = n2j(node.operand);
        break;
      case ts.SyntaxKind.PostfixUnaryExpression:
        r.operator = ts.SyntaxKind[node.operator];
        r.operand = n2j(node.operand);
        break;
      case ts.SyntaxKind.CallExpression:
        r.expression = n2j(node.expression);
        r.arguments = node.arguments.map(n2j);
        if (node.typeArguments) r.typeArguments = node.typeArguments.map(n2j);
        break;
      case ts.SyntaxKind.PropertyAccessExpression:
        r.expression = n2j(node.expression);
        r.name = n2j(node.name);
        if (node.questionDotToken) r.optional = true;
        break;
      case ts.SyntaxKind.ElementAccessExpression:
        r.expression = n2j(node.expression);
        r.argumentExpression = n2j(node.argumentExpression);
        if (node.questionDotToken) r.optional = true;
        break;
      case ts.SyntaxKind.IfStatement:
        r.expression = n2j(node.expression);
        r.thenStatement = n2j(node.thenStatement);
        if (node.elseStatement) r.elseStatement = n2j(node.elseStatement);
        break;
      case ts.SyntaxKind.WhileStatement:
      case ts.SyntaxKind.DoStatement:
        r.expression = n2j(node.expression);
        r.statement = n2j(node.statement);
        break;
      case ts.SyntaxKind.ForStatement:
        if (node.initializer) r.initializer = n2j(node.initializer);
        if (node.condition) r.condition = n2j(node.condition);
        if (node.incrementor) r.incrementor = n2j(node.incrementor);
        r.statement = n2j(node.statement);
        break;
      case ts.SyntaxKind.ForOfStatement:
      case ts.SyntaxKind.ForInStatement:
        r.initializer = n2j(node.initializer);
        r.expression = n2j(node.expression);
        r.statement = n2j(node.statement);
        break;
      case ts.SyntaxKind.NewExpression:
        r.expression = n2j(node.expression);
        r.arguments = node.arguments ? node.arguments.map(n2j) : [];
        if (node.typeArguments) r.typeArguments = node.typeArguments.map(n2j);
        break;
      case ts.SyntaxKind.ClassDeclaration:
        if (node.name) r.name = n2j(node.name);
        r.members = node.members.map(n2j);
        if (node.heritageClauses)
          r.heritage = node.heritageClauses.map(n2j);
        if (node.typeParameters)
          r.typeParameters = node.typeParameters.map(n2j);
        break;
      case ts.SyntaxKind.ShorthandPropertyAssignment:
        r.name = n2j(node.name);
        break;
      case ts.SyntaxKind.PropertyDeclaration:
      case ts.SyntaxKind.PropertySignature:
        r.name = n2j(node.name);
        if (node.type) r.typeAnnotation = n2j(node.type);
        if (node.initializer) r.initializer = n2j(node.initializer);
        if (node.questionToken) r.optional = true;
        if (node.name && node.name.text && node.name.text.startsWith("#"))
          r.isPrivate = true;
        if (
          node.modifiers &&
          node.modifiers.some(
            (m) => m.kind === ts.SyntaxKind.PrivateKeyword
          )
        )
          r.isPrivate = true;
        break;
      case ts.SyntaxKind.Constructor:
        r.parameters = node.parameters.map(n2j);
        if (node.body) r.body = n2j(node.body);
        break;
      case ts.SyntaxKind.MethodDeclaration:
      case ts.SyntaxKind.GetAccessor:
      case ts.SyntaxKind.SetAccessor:
        if (node.kind === ts.SyntaxKind.GetAccessor) r.isGetter = true;
        if (node.kind === ts.SyntaxKind.SetAccessor) r.isSetter = true;
        if (node.name && node.name.text)
          r.name = { _kind: "Identifier", text: node.name.text };
        else if (node.name) r.name = n2j(node.name);
        r.parameters = node.parameters
          ? Array.from(node.parameters).map(n2j)
          : [];
        if (node.type) r.returnType = n2j(node.type);
        if (node.body) r.body = n2j(node.body);
        if (
          node.modifiers &&
          node.modifiers.some((m) => m.kind === ts.SyntaxKind.StaticKeyword)
        )
          r.isStatic = true;
        try {
          const sig = checker.getSignatureFromDeclaration(node);
          if (sig) {
            r._returnType = checker.typeToString(sig.getReturnType());
            r._paramTypes = sig
              .getParameters()
              .map((p) =>
                checker.typeToString(
                  checker.getTypeOfSymbolAtLocation(p, node)
                )
              );
          }
        } catch (e) {}
        break;
      case ts.SyntaxKind.ThisKeyword:
        r.text = "this";
        break;
      case ts.SyntaxKind.TemplateExpression:
        r.head = n2j(node.head);
        r.templateSpans = node.templateSpans.map(n2j);
        break;
      case ts.SyntaxKind.TaggedTemplateExpression:
        r.tag = n2j(node.tag);
        r.template = n2j(node.template);
        break;
      case ts.SyntaxKind.TemplateHead:
      case ts.SyntaxKind.TemplateMiddle:
      case ts.SyntaxKind.TemplateTail:
        r.text = node.text;
        break;
      case ts.SyntaxKind.TemplateSpan:
        r.expression = n2j(node.expression);
        r.literal = n2j(node.literal);
        break;
      case ts.SyntaxKind.ArrayLiteralExpression:
        r.elements = node.elements.map(n2j);
        break;
      case ts.SyntaxKind.ObjectLiteralExpression:
        r.properties = node.properties.map(n2j);
        break;
      case ts.SyntaxKind.PropertyAssignment:
        r.name = n2j(node.name);
        r.initializer = n2j(node.initializer);
        break;
      case ts.SyntaxKind.SwitchStatement:
        r.expression = n2j(node.expression);
        r.caseBlock = n2j(node.caseBlock);
        break;
      case ts.SyntaxKind.CaseBlock:
        r.clauses = node.clauses.map(n2j);
        break;
      case ts.SyntaxKind.CaseClause:
        r.expression = n2j(node.expression);
        r.statements = node.statements.map(n2j);
        break;
      case ts.SyntaxKind.DefaultClause:
        r.statements = node.statements.map(n2j);
        break;
      case ts.SyntaxKind.ObjectBindingPattern:
      case ts.SyntaxKind.ArrayBindingPattern:
        r.elements = node.elements.map(n2j);
        break;
      case ts.SyntaxKind.BindingElement:
        r.name = n2j(node.name);
        if (node.propertyName) r.propertyName = n2j(node.propertyName);
        if (node.initializer) r.initializer = n2j(node.initializer);
        if (node.dotDotDotToken) r.isRest = true;
        break;
      case ts.SyntaxKind.EnumDeclaration:
      case ts.SyntaxKind.InterfaceDeclaration:
        if (node.name) r.name = n2j(node.name);
        r.members = node.members.map(n2j);
        break;
      case ts.SyntaxKind.EnumMember:
        r.name = n2j(node.name);
        if (node.initializer) r.initializer = n2j(node.initializer);
        break;
      case ts.SyntaxKind.TryStatement:
        r.tryBlock = n2j(node.tryBlock);
        if (node.catchClause) r.catchClause = n2j(node.catchClause);
        if (node.finallyBlock) r.finallyBlock = n2j(node.finallyBlock);
        break;
      case ts.SyntaxKind.CatchClause:
        if (node.variableDeclaration)
          r.variableDeclaration = n2j(node.variableDeclaration);
        r.block = n2j(node.block);
        break;
      case ts.SyntaxKind.ImportDeclaration:
        if (node.importClause) r.importClause = n2j(node.importClause);
        if (node.moduleSpecifier)
          r.moduleSpecifier = n2j(node.moduleSpecifier);
        break;
      case ts.SyntaxKind.ExportDeclaration:
      case ts.SyntaxKind.ExportAssignment:
        break;
      case ts.SyntaxKind.ImportClause:
        if (node.namedBindings) r.namedBindings = n2j(node.namedBindings);
        if (node.name) r.name = n2j(node.name);
        break;
      case ts.SyntaxKind.NamedImports:
        r.elements = node.elements.map(n2j);
        break;
      case ts.SyntaxKind.ImportSpecifier:
        r.name = n2j(node.name);
        if (node.propertyName) r.propertyName = n2j(node.propertyName);
        break;
      case ts.SyntaxKind.ThrowStatement:
        r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.SpreadElement:
      case ts.SyntaxKind.SpreadAssignment:
        r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.TypeOfExpression:
        r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.BreakStatement:
      case ts.SyntaxKind.ContinueStatement:
        if (node.label) r.label = node.label.text;
        break;
      case ts.SyntaxKind.LabeledStatement:
        r.label = node.label.text;
        r.statement = n2j(node.statement);
        break;
      case ts.SyntaxKind.TypeAliasDeclaration:
        if (node.name) r.name = n2j(node.name);
        if (node.type) {
          try {
            r.aliasedType = node.type.getText();
            const rhs_type = checker.getTypeFromTypeNode(node.type);
            const resolved = checker.typeToString(
              rhs_type,
              node,
              ts.TypeFormatFlags.NoTruncation |
                ts.TypeFormatFlags.InTypeAlias |
                ts.TypeFormatFlags.UseFullyQualifiedType
            );
            r.resolvedType = resolved;
          } catch (e) {}
        }
        break;
      case ts.SyntaxKind.AsExpression:
      case ts.SyntaxKind.ParenthesizedExpression:
      case ts.SyntaxKind.VoidExpression:
      case ts.SyntaxKind.DeleteExpression:
      case ts.SyntaxKind.AwaitExpression:
      case ts.SyntaxKind.NonNullExpression:
        r.expression = n2j(node.expression);
        break;
      case ts.SyntaxKind.ConditionalExpression:
        r.condition = n2j(node.condition);
        r.whenTrue = n2j(node.whenTrue);
        r.whenFalse = n2j(node.whenFalse);
        break;
      default:
        ts.forEachChild(node, (child) => {
          if (!r._children) r._children = [];
          r._children.push(n2j(child));
        });
        break;
    }
    return r;
  }
  return n2j;
}

function parseFile(inputFile) {
  const content = fs.readFileSync(inputFile, "utf-8");
  // Only keep the current file in the map. Otherwise the Language
  // Service re-type-checks every file ever registered on each call,
  // giving quadratic behaviour across many parses.
  files = {};
  files[inputFile] = { content, version: ++version };
  const program = ls.getProgram();
  if (!program) throw new Error("Language service returned no program");
  const sourceFile = program.getSourceFile(inputFile);
  if (!sourceFile)
    throw new Error("Source file not loaded: " + inputFile);
  const checker = program.getTypeChecker();
  const n2j = makeN2j(sourceFile, checker);
  const allFiles = program
    .getSourceFiles()
    .filter(
      (sf) => !sf.isDeclarationFile && !sf.fileName.includes("node_modules")
    );
  const combined = { _kind: "SourceFile", statements: [] };
  for (const sf of allFiles) {
    for (const stmt of sf.statements) {
      combined.statements.push(n2j(stmt));
    }
  }
  combined._type = "void";
  combined._pos = { line: 1, col: 0 };
  return combined;
}

// Clean up any pre-existing socket
try {
  fs.unlinkSync(socketPath);
} catch (e) {}

const server = net.createServer((conn) => {
  let buffer = Buffer.alloc(0);
  let handled = false;

  const respond = (obj) => {
    if (handled) return;
    handled = true;
    const payload = Buffer.from(JSON.stringify(obj), "utf-8");
    const header = Buffer.from(payload.length.toString() + "\n", "ascii");
    conn.write(header);
    conn.write(payload);
    conn.write("\n");
    conn.end();
  };

  conn.on("data", (chunk) => {
    if (handled) return;
    buffer = Buffer.concat([buffer, chunk]);
    const nl = buffer.indexOf(0x0a);
    if (nl < 0) return; // wait for more
    const requestPath = buffer.subarray(0, nl).toString("utf-8");
    try {
      const ast = parseFile(requestPath);
      respond(ast);
    } catch (e) {
      respond({ _kind: "Error", message: e.message });
    }
  });

  conn.on("end", () => {
    if (!handled) respond({ _kind: "Error", message: "empty request" });
  });

  conn.on("error", () => {
    handled = true;
  });
});

server.listen(socketPath, () => {
  // Print readiness marker so the wrapper can detect the server is up.
  process.stdout.write("READY\n");
});

// Graceful shutdown
function shutdown() {
  server.close(() => {
    try {
      fs.unlinkSync(socketPath);
    } catch (e) {}
    process.exit(0);
  });
}
process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);
