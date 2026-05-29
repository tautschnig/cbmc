/// \file
/// TypeScript language frontend for CBMC — implementation
///
/// Cross-references:
/// - ES2024 sec-scripts-and-modules (module parsing)
/// - ES2024 sec-ecmascript-language-source-code (source text)
/// - TSH: Modules (import/export resolution via TS Compiler API)

#include "typescript_language.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/message.h>
#include <util/options.h>
#include <util/run.h>
#include <util/std_expr.h>
#include <util/suffix.h>
#include <util/symbol.h>
#include <util/tempfile.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "expr2typescript.h"
#include "typescript_converter.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <vector>

std::unique_ptr<languaget> new_typescript_language()
{
  return std::make_unique<typescript_languaget>();
}

void typescript_languaget::set_language_options(
  const optionst &_options,
  message_handlert &message_handler)
{
  options = &_options;
}

/// Parse a TypeScript file by invoking Node.js with the TypeScript
/// Compiler API to produce a JSON AST with type annotations.
bool typescript_languaget::parse(
  std::istream &instream,
  const std::string &path,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  filename = path;

  // Create a temporary file for the JSON AST output
  temporary_filet json_tmp{"cbmc_ts_ast_", ".json"};
  std::string json_path = json_tmp();

  // Use an inline Node.js script that uses the TypeScript
  // Compiler API to parse and type-check the source file.

  // clang-format off
  std::string ts_script = R"JS(
const ts = require("/usr/local/lib/node_modules/typescript");
const fs = require("fs");
const inputFile = process.argv[2];
// ES2024 sec-scripts-and-modules: parse source into AST via TypeScript Compiler API
const outputFile = process.argv[3];
const program = ts.createProgram([inputFile], {
  target: ts.ScriptTarget.ES2022,
  module: ts.ModuleKind.CommonJS,
  strict: true,
  noEmit: true,
  skipLibCheck: true,
  jsx: ts.JsxEmit.React,
});
const checker = program.getTypeChecker();
const sourceFile = program.getSourceFile(inputFile);
if (!sourceFile) { process.exit(1); }

// Surface syntax errors with file:line:col before walking the AST.
// (Parallels the daemon's handling — see ts_ast_server.js.)
const syntaxDiags = program.getSyntacticDiagnostics(sourceFile);
if (syntaxDiags && syntaxDiags.length > 0) {
  const lines = syntaxDiags.slice(0, 5).map((d) => {
    let where = inputFile;
    if (d.file && d.start !== undefined) {
      const lc = d.file.getLineAndCharacterOfPosition(d.start);
      where = d.file.fileName + ":" + (lc.line + 1) + ":" + (lc.character + 1);
    }
    const text = ts.flattenDiagnosticMessageText(d.messageText, "\n");
    return where + ": " + text;
  });
  const more = syntaxDiags.length > 5
    ? "\n  (and " + (syntaxDiags.length - 5) + " more)"
    : "";
  process.stderr.write(
    "TypeScript syntax error(s):\n  " + lines.join("\n  ") + more + "\n"
  );
  process.exit(2);
}
function n2j(node) {
  const r = {
    _kind: ts.SyntaxKind[node.kind],
    _pos: {
      line: sourceFile.getLineAndCharacterOfPosition(node.getStart()).line + 1,
      col: sourceFile.getLineAndCharacterOfPosition(node.getStart()).character,
    },
  };
  try { r._type = checker.typeToString(checker.getTypeAtLocation(node)); } catch(e) {}
  switch(node.kind) {
    case ts.SyntaxKind.SourceFile:
      r.statements = node.statements.map(n2j);
      break;
    case ts.SyntaxKind.VariableStatement:
      r.declarationList = n2j(node.declarationList);
      break;
    case ts.SyntaxKind.VariableDeclarationList:
      r.declarations = node.declarations.map(n2j);
      r.flags = node.flags & ts.NodeFlags.Let ? "let" :
                node.flags & ts.NodeFlags.Const ? "const" : "var";
      break;
    case ts.SyntaxKind.VariableDeclaration:
      r.name = n2j(node.name);
      if(node.type) r.typeAnnotation = n2j(node.type);
      if(node.initializer) r.initializer = n2j(node.initializer);
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
    case ts.SyntaxKind.TrueKeyword: r.value = true; break;
    case ts.SyntaxKind.FalseKeyword: r.value = false; break;
    case ts.SyntaxKind.NullKeyword: r.value = null; break;
    case ts.SyntaxKind.FunctionDeclaration:
    case ts.SyntaxKind.ArrowFunction:
    case ts.SyntaxKind.FunctionExpression:
      if(node.name) r.name = n2j(node.name);
      if(node.asteriskToken) r.isGenerator = true;
      r.parameters = node.parameters.map(n2j);
      if(node.type) r.returnType = n2j(node.type);
      if(node.typeParameters) r.typeParameters = node.typeParameters.map(n2j);
      if(node.body) r.body = n2j(node.body);
      try {
        const sig = checker.getSignatureFromDeclaration(node);
        if(sig) {
          r._returnType = checker.typeToString(sig.getReturnType());
          r._paramTypes = sig.getParameters().map(p =>
            checker.typeToString(checker.getTypeOfSymbolAtLocation(p, node)));
        }
      } catch(e) {}
      break;
    case ts.SyntaxKind.Parameter:
      r.name = n2j(node.name);
      if(node.type) r.typeAnnotation = n2j(node.type);
      if(node.initializer) r.initializer = n2j(node.initializer);
      if(node.dotDotDotToken) r.isRest = true;
      if(node.modifiers && node.modifiers.length > 0)
        r.modifiers = node.modifiers.map((m) => ({
          _kind: ts.SyntaxKind[m.kind],
        }));
      break;
    case ts.SyntaxKind.Block:
      r.statements = node.statements.map(n2j);
      break;
    case ts.SyntaxKind.ReturnStatement:
      if(node.expression) r.expression = n2j(node.expression);
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
      // ES2024 §13.3.10: Dynamic import — `import("./mod")`. Detect
      // by checking whether the callee is the `import` keyword.
      // When detected, also emit the resolved module's exported
      // names + their types so the converter can build a struct of
      // the proper shape.
      if(node.expression.kind === ts.SyntaxKind.ImportKeyword) {
        r.dynamicImport = true;
        try {
          const moduleType = checker.getTypeAtLocation(node);
          // The Promise<typeof import(...)> wrapper: peel it.
          let inner = moduleType;
          if(moduleType.symbol && moduleType.symbol.name === "Promise" &&
             moduleType.aliasTypeArguments &&
             moduleType.aliasTypeArguments.length === 1) {
            inner = moduleType.aliasTypeArguments[0];
          } else if(moduleType.typeArguments && moduleType.typeArguments.length === 1) {
            inner = moduleType.typeArguments[0];
          }
          // Inner is now `typeof import("...")` — get its symbol's exports.
          const exportsList = [];
          if(inner.symbol) {
            const moduleExports = checker.getExportsOfModule(inner.symbol);
            for(const exp of moduleExports) {
              const expType = checker.getTypeOfSymbolAtLocation(exp, node);
              exportsList.push({
                name: exp.name,
                type: checker.typeToString(expType, node),
              });
            }
          }
          r.moduleExports = exportsList;
        } catch(e) {}
      }
      break;
    case ts.SyntaxKind.PropertyAccessExpression:
      r.expression = n2j(node.expression);
      r.name = n2j(node.name);
      if(node.questionDotToken) r.optional = true;
      break;
    case ts.SyntaxKind.ElementAccessExpression:
      // ES2024 §6.1.5.1: `obj[Symbol.X]` — reserialise as the
      // synthetic identifier `@@X` so that the property access
      // resolves to the method-of-the-same-name registered by
      // ComputedPropertyName above. After the rewrite, the access
      // is equivalent to a PropertyAccessExpression — emit it as
      // such so the converter's existing method-call dispatch
      // path picks it up unchanged.
      {
        const arg = node.argumentExpression;
        if(arg && arg.kind === ts.SyntaxKind.PropertyAccessExpression &&
           arg.expression && arg.expression.kind === ts.SyntaxKind.Identifier &&
           arg.expression.escapedText === "Symbol" &&
           arg.name && arg.name.escapedText) {
          // Rewrite to PropertyAccessExpression(@@X)
          r._kind = "PropertyAccessExpression";
          r.expression = n2j(node.expression);
          r.name = { _kind: "Identifier", text: "@@" + arg.name.escapedText };
          if(node.questionDotToken) r.optional = true;
          break;
        }
      }
      r.expression = n2j(node.expression);
      r.argumentExpression = n2j(node.argumentExpression);
      if(node.questionDotToken) r.optional = true;
      break;
    case ts.SyntaxKind.IfStatement:
      r.expression = n2j(node.expression);
      r.thenStatement = n2j(node.thenStatement);
      if(node.elseStatement) r.elseStatement = n2j(node.elseStatement);
      break;
    case ts.SyntaxKind.WhileStatement:
    case ts.SyntaxKind.DoStatement:
      r.expression = n2j(node.expression);
      r.statement = n2j(node.statement);
      break;
    case ts.SyntaxKind.ForStatement:
      if(node.initializer) r.initializer = n2j(node.initializer);
      if(node.condition) r.condition = n2j(node.condition);
      if(node.incrementor) r.incrementor = n2j(node.incrementor);
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
      if(node.typeArguments) r.typeArguments = node.typeArguments.map(n2j);
      break;
    case ts.SyntaxKind.ClassDeclaration:
      if(node.name) r.name = n2j(node.name);
      r.members = node.members.map(n2j);
      if(node.heritageClauses) r.heritage = node.heritageClauses.map(n2j);
      if(node.typeParameters) r.typeParameters = node.typeParameters.map(n2j);
      break;
    case ts.SyntaxKind.ShorthandPropertyAssignment:
      r.name = n2j(node.name);
      break;
    case ts.SyntaxKind.PropertyDeclaration:
    case ts.SyntaxKind.PropertySignature:
      r.name = n2j(node.name);
      if(node.type) r.typeAnnotation = n2j(node.type);
      if(node.initializer) r.initializer = n2j(node.initializer);
      if(node.questionToken) r.optional = true;
      if(node.name && node.name.text && node.name.text.startsWith('#'))
        r.isPrivate = true;
      if(node.modifiers && node.modifiers.some(m => m.kind === ts.SyntaxKind.PrivateKeyword))
        r.isPrivate = true;
      break;
    case ts.SyntaxKind.Constructor:
      r.parameters = node.parameters.map(n2j);
      if(node.body) r.body = n2j(node.body);
      break;
    case ts.SyntaxKind.MethodDeclaration:
    case ts.SyntaxKind.GetAccessor:
    case ts.SyntaxKind.SetAccessor:
      if(node.kind === ts.SyntaxKind.GetAccessor) r.isGetter = true;
      if(node.kind === ts.SyntaxKind.SetAccessor) r.isSetter = true;
      // ES2024 §6.1.5.1: well-known symbols (Symbol.iterator,
      // Symbol.toPrimitive, etc.). Computed property names of the
      // form `[Symbol.X]` are reserialised as a synthetic
      // identifier `@@X` so the rest of the frontend can treat
      // symbol-keyed methods as named members.
      if(node.name && node.name.kind === ts.SyntaxKind.ComputedPropertyName) {
        const inner = node.name.expression;
        if(inner && inner.kind === ts.SyntaxKind.PropertyAccessExpression &&
           inner.expression && inner.expression.kind === ts.SyntaxKind.Identifier &&
           inner.expression.escapedText === "Symbol" &&
           inner.name && inner.name.escapedText) {
          r.name = { _kind: "Identifier", text: "@@" + inner.name.escapedText };
        } else {
          r.name = n2j(node.name);
        }
      } else if(node.name && node.name.text) r.name = { _kind: "Identifier", text: node.name.text };
      else if(node.name) r.name = n2j(node.name);
      r.parameters = node.parameters ? Array.from(node.parameters).map(n2j) : [];
      if(node.type) r.returnType = n2j(node.type);
      if(node.body) r.body = n2j(node.body);
      if(node.modifiers && node.modifiers.some(m => m.kind === ts.SyntaxKind.StaticKeyword))
        r.isStatic = true;
      try {
        const sig = checker.getSignatureFromDeclaration(node);
        if(sig) {
          r._returnType = checker.typeToString(sig.getReturnType());
          r._paramTypes = sig.getParameters().map(p =>
            checker.typeToString(checker.getTypeOfSymbolAtLocation(p, node)));
        }
      } catch(e) {}
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
      if(node.propertyName) r.propertyName = n2j(node.propertyName);
      if(node.initializer) r.initializer = n2j(node.initializer);
      if(node.dotDotDotToken) r.isRest = true;
      break;
    case ts.SyntaxKind.EnumDeclaration:
    case ts.SyntaxKind.InterfaceDeclaration:
      if(node.name) r.name = n2j(node.name);
      r.members = node.members.map(n2j);
      break;
    case ts.SyntaxKind.EnumMember:
      r.name = n2j(node.name);
      if(node.initializer) r.initializer = n2j(node.initializer);
      break;
    case ts.SyntaxKind.TryStatement:
      r.tryBlock = n2j(node.tryBlock);
      if(node.catchClause) r.catchClause = n2j(node.catchClause);
      if(node.finallyBlock) r.finallyBlock = n2j(node.finallyBlock);
      break;
    case ts.SyntaxKind.CatchClause:
      if(node.variableDeclaration) r.variableDeclaration = n2j(node.variableDeclaration);
      r.block = n2j(node.block);
      break;
    case ts.SyntaxKind.ImportDeclaration:
      if(node.importClause) r.importClause = n2j(node.importClause);
      if(node.moduleSpecifier) r.moduleSpecifier = n2j(node.moduleSpecifier);
      break;
    case ts.SyntaxKind.ExportDeclaration:
    case ts.SyntaxKind.ExportAssignment:
      break;
    case ts.SyntaxKind.ImportClause:
      if(node.namedBindings) r.namedBindings = n2j(node.namedBindings);
      if(node.name) r.name = n2j(node.name);
      break;
    case ts.SyntaxKind.NamedImports:
      r.elements = node.elements.map(n2j);
      break;
    case ts.SyntaxKind.ImportSpecifier:
      r.name = n2j(node.name);
      if(node.propertyName) r.propertyName = n2j(node.propertyName);
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
    case ts.SyntaxKind.ImportDeclaration:
      if(node.importClause) r.importClause = n2j(node.importClause);
      if(node.moduleSpecifier) r.moduleSpecifier = n2j(node.moduleSpecifier);
      break;
    case ts.SyntaxKind.ExportDeclaration:
    case ts.SyntaxKind.ExportAssignment:
      break;
    case ts.SyntaxKind.ImportClause:
      if(node.namedBindings) r.namedBindings = n2j(node.namedBindings);
      if(node.name) r.name = n2j(node.name);
      break;
    case ts.SyntaxKind.NamedImports:
      r.elements = node.elements.map(n2j);
      break;
    case ts.SyntaxKind.ImportSpecifier:
      r.name = n2j(node.name);
      if(node.propertyName) r.propertyName = n2j(node.propertyName);
      break;
    case ts.SyntaxKind.ThrowStatement:
      r.expression = n2j(node.expression);
      break;
    case ts.SyntaxKind.BreakStatement:
    case ts.SyntaxKind.ContinueStatement:
      if(node.label) r.label = node.label.text;
      break;
    case ts.SyntaxKind.LabeledStatement:
      r.label = node.label.text;
      r.statement = n2j(node.statement);
      break;
    case ts.SyntaxKind.TypeAliasDeclaration:
      if(node.name) r.name = n2j(node.name);
      if(node.type) {
        try {
          // Two approaches: source text (preserves discriminated unions)
          // and resolved type (handles utility types like Pick, Omit).
          // We emit both; converter prefers resolvedType if it's not the alias name.
          r.aliasedType = node.type.getText();
          const rhs_type = checker.getTypeFromTypeNode(node.type);
          const resolved = checker.typeToString(
            rhs_type, node,
            ts.TypeFormatFlags.NoTruncation |
            ts.TypeFormatFlags.InTypeAlias |
            ts.TypeFormatFlags.UseFullyQualifiedType);
          r.resolvedType = resolved;
        } catch(e) {}
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
      ts.forEachChild(node, child => {
        if(!r._children) r._children = [];
        r._children.push(n2j(child));
      });
      break;
  }
  return r;
}
fs.writeFileSync(outputFile, JSON.stringify((() => {
  // Multi-file support: include all non-declaration source files
  const allFiles = program.getSourceFiles().filter(
    sf => !sf.isDeclarationFile && !sf.fileName.includes('node_modules'));
  // Process imported files first, entry file last
  const combined = { _kind: "SourceFile", statements: [] };
  for (const sf of allFiles) {
    // Set sourceFile for position tracking
    const prevSF = sourceFile;
    for (const stmt of sf.statements) {
      combined.statements.push(n2j(stmt));
    }
  }
  combined._type = "void";
  combined._pos = { line: 1, col: 0 };
  return combined;
})()));
)JS";
  // clang-format on

  // Daemon fast path: if CBMC_TS_SERVER_SOCKET points at a live
  // Unix-domain socket exposed by scripts/cbmc_ts_server, send the
  // source path there and read back the JSON AST. Avoids per-call
  // node/tsc startup (~900 ms → ~50 ms). The daemon caches lib.d.ts
  // across connections via the TypeScript Language Service.
  if(const char *sock_env = std::getenv("CBMC_TS_SERVER_SOCKET"))
  {
    std::string sock_path = sock_env;
    if(!sock_path.empty())
    {
      int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if(fd >= 0)
      {
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if(sock_path.size() < sizeof(addr.sun_path))
        {
          std::memcpy(addr.sun_path, sock_path.data(), sock_path.size());
          if(::connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
          {
            // Resolve to absolute so the daemon (which has its own
            // cwd) can find the file.
            std::string abs_path;
            char *rp = ::realpath(path.c_str(), nullptr);
            if(rp != nullptr)
            {
              abs_path = rp;
              std::free(rp);
            }
            else
            {
              abs_path = path;
            }
            // Send "<path>\n".
            std::string req = abs_path + "\n";
            ssize_t sent = ::write(fd, req.data(), req.size());
            if(sent == static_cast<ssize_t>(req.size()))
            {
              // Read header "<length>\n" then payload.
              std::string header;
              char hb;
              while(::read(fd, &hb, 1) == 1 && hb != '\n')
                header.push_back(hb);
              std::size_t length = 0;
              try
              {
                length = std::stoul(header);
              }
              catch(...)
              {
                length = 0;
              }
              std::string payload;
              payload.reserve(length);
              std::vector<char> buf(4096);
              while(payload.size() < length)
              {
                ssize_t n = ::read(
                  fd,
                  buf.data(),
                  std::min<std::size_t>(buf.size(), length - payload.size()));
                if(n <= 0)
                  break;
                payload.append(buf.data(), buf.data() + n);
              }
              ::close(fd);
              if(payload.size() == length && length > 0)
              {
                std::istringstream iss{payload};
                if(parse_json(iss, path, message_handler, ast_json))
                {
                  log.error()
                    << "Failed to parse JSON from daemon" << messaget::eom;
                  return true;
                }
                // Detect the server's error-envelope (returned when
                // TS type-check failed).
                if(ast_json.is_object())
                {
                  const auto &obj = static_cast<const json_objectt &>(ast_json);
                  auto kind_it = obj.find("_kind");
                  if(
                    kind_it != obj.end() && kind_it->second.is_string() &&
                    kind_it->second.value == "Error")
                  {
                    auto msg_it = obj.find("message");
                    log.error()
                      << "Daemon parse error: "
                      << (msg_it != obj.end() && msg_it->second.is_string()
                            ? msg_it->second.value
                            : std::string{"unknown"})
                      << messaget::eom;
                    return true;
                  }
                }
                return false;
              }
              // Fall through to one-shot node on any framing error.
            }
            else
            {
              ::close(fd);
            }
          }
          else
          {
            ::close(fd);
          }
        }
        else
        {
          ::close(fd);
        }
      }
      // Any error with the daemon path: fall back to one-shot node
      // below (unchanged behaviour). This keeps the daemon fully
      // optional.
    }
  }

  // Write the TypeScript AST converter script to a temp file
  temporary_filet script_tmp{"cbmc_ts_script_", ".js"};
  std::string script_path = script_tmp();
  {
    std::ofstream script_out{script_path};
    script_out << ts_script;
  }

  // Invoke Node.js with the script
  temporary_filet stderr_tmp{"cbmc_ts_err_", ".txt"};
  std::string stderr_path = stderr_tmp();

  int result =
    run("node", {"node", script_path, path, json_path}, "", "", stderr_path);

  if(result != 0)
  {
    // Surface the node script's stderr (which contains
    // file:line:col diagnostics for syntax errors) so the user has
    // enough info to fix the source.
    std::ifstream err_in{stderr_path};
    std::string err_msg{
      std::istreambuf_iterator<char>{err_in}, std::istreambuf_iterator<char>{}};
    while(!err_msg.empty() && err_msg.back() == '\n')
      err_msg.pop_back();
    if(!err_msg.empty())
      log.error() << err_msg << messaget::eom;
    log.error() << "Failed to parse TypeScript file: " << path << messaget::eom;
    return true;
  }

  // Read the JSON AST
  std::ifstream json_file{json_path};
  if(!json_file)
  {
    log.error() << "Failed to read JSON AST: " << json_path << messaget::eom;
    return true;
  }

  if(parse_json(json_file, json_path, message_handler, ast_json))
  {
    log.error() << "Failed to parse JSON AST" << messaget::eom;
    return true;
  }

  return false;
}

bool typescript_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  log.status() << "Type-checking " << filename << messaget::eom;

  typescript_convertert converter{
    symbol_table, filename, ast_json, message_handler};
  if(options != nullptr)
  {
    converter.bounds_check = options->get_bool_option("bounds-check");
    converter.div_by_zero_check =
      options->get_bool_option("float-div-by-zero-check");
    converter.nan_check = options->get_bool_option("nan-check");
    converter.integer_inference = options->get_bool_option("ts-integer-mode");
    converter.async_threading = options->get_bool_option("ts-async-threading");
    converter.bigint_mathematical =
      options->get_bool_option("ts-bigint-mathematical");
    if(options->is_set("ts-max-array-size"))
      converter.TYPESCRIPT_MAX_ARRAY_LENGTH =
        options->get_unsigned_int_option("ts-max-array-size");
  }
  if(converter.convert())
    return true;

  // Create __CPROVER_rounding_mode (needed for float operations)
  irep_idt rm_id{"__CPROVER_rounding_mode"};
  if(symbol_table.lookup(rm_id) == nullptr)
  {
    symbolt rm_sym{rm_id, signedbv_typet{32}, "typescript"};
    rm_sym.base_name = "__CPROVER_rounding_mode";
    rm_sym.is_lvalue = true;
    rm_sym.is_state_var = true;
    rm_sym.is_static_lifetime = true;
    rm_sym.value = from_integer(0, signedbv_typet{32});
    symbol_table.add(rm_sym);
  }

  return false;
}

bool typescript_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  // Create __CPROVER_rounding_mode (needed for float operations)
  irep_idt rm_id{"__CPROVER_rounding_mode"};
  if(symbol_table.lookup(rm_id) == nullptr)
  {
    symbolt rm_sym{rm_id, signedbv_typet{32}, "typescript"};
    rm_sym.base_name = "__CPROVER_rounding_mode";
    rm_sym.is_lvalue = true;
    rm_sym.is_state_var = true;
    rm_sym.is_static_lifetime = true;
    rm_sym.value = from_integer(0, signedbv_typet{32}); // round to nearest
    symbol_table.add(rm_sym);
  }
  return false;
}

bool typescript_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2typescript(expr, ns);
  return false;
}

bool typescript_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2typescript(type, ns);
  return false;
}

bool typescript_languaget::to_expr(
  const std::string &code,
  const std::string &module,
  exprt &expr,
  const namespacet &ns,
  message_handlert &message_handler)
{
  return true; // not implemented
}

void typescript_languaget::show_parse(
  std::ostream &out,
  message_handlert &message_handler)
{
  out << ast_json << "\n";
}
