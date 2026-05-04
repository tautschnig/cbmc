// TypeScript AST to JSON converter for CBMC
// Uses the TypeScript Compiler API to parse, type-check, and emit a JSON AST
// with resolved type information for every node.
//
// Usage: node ts_ast_to_json.js <input.ts> <output.json>

const ts = require("/usr/local/lib/node_modules/typescript");
const fs = require("fs");
const path = require("path");

const inputFile = process.argv[2];
const outputFile = process.argv[3];

if (!inputFile || !outputFile) {
  console.error("Usage: node ts_ast_to_json.js <input.ts> <output.json>");
  process.exit(1);
}

// Create a program with strict type checking
const compilerOptions = {
  target: ts.ScriptTarget.ES2022,
  module: ts.ModuleKind.CommonJS,
  strict: true,
  noEmit: true,
  skipLibCheck: true,
};

const program = ts.createProgram([inputFile], compilerOptions);
const checker = program.getTypeChecker();
const sourceFile = program.getSourceFile(inputFile);

if (!sourceFile) {
  console.error("Failed to parse: " + inputFile);
  process.exit(1);
}

// Convert a TypeScript AST node to a JSON-serializable object
function nodeToJson(node) {
  const result = {
    _kind: ts.SyntaxKind[node.kind],
    _pos: {
      line: sourceFile.getLineAndCharacterOfPosition(node.getStart()).line + 1,
      col: sourceFile.getLineAndCharacterOfPosition(node.getStart()).character,
    },
  };

  // Get the resolved type for expressions
  try {
    const type = checker.getTypeAtLocation(node);
    if (type) {
      result._type = checker.typeToString(type);
    }
  } catch (e) {
    // Some nodes don't have types
  }

  // Node-specific fields
  switch (node.kind) {
    case ts.SyntaxKind.SourceFile:
      result.statements = node.statements.map(nodeToJson);
      result.fileName = node.fileName;
      break;

    case ts.SyntaxKind.VariableStatement:
      result.declarationList = nodeToJson(node.declarationList);
      break;

    case ts.SyntaxKind.VariableDeclarationList:
      result.declarations = node.declarations.map(nodeToJson);
      result.flags = node.flags & ts.NodeFlags.Let ? "let" :
                     node.flags & ts.NodeFlags.Const ? "const" : "var";
      break;

    case ts.SyntaxKind.VariableDeclaration:
      result.name = nodeToJson(node.name);
      if (node.type) result.typeAnnotation = nodeToJson(node.type);
      if (node.initializer) result.initializer = nodeToJson(node.initializer);
      break;

    case ts.SyntaxKind.Identifier:
      result.text = node.text;
      break;

    case ts.SyntaxKind.NumericLiteral:
      result.text = node.text;
      result.value = parseFloat(node.text);
      break;

    case ts.SyntaxKind.StringLiteral:
    case ts.SyntaxKind.NoSubstitutionTemplateLiteral:
      result.text = node.text;
      break;

    case ts.SyntaxKind.TrueKeyword:
      result.value = true;
      break;

    case ts.SyntaxKind.FalseKeyword:
      result.value = false;
      break;

    case ts.SyntaxKind.NullKeyword:
      result.value = null;
      break;

    case ts.SyntaxKind.FunctionDeclaration:
    case ts.SyntaxKind.MethodDeclaration:
    case ts.SyntaxKind.ArrowFunction:
    case ts.SyntaxKind.FunctionExpression:
      if (node.name) result.name = nodeToJson(node.name);
      result.parameters = node.parameters.map(nodeToJson);
      if (node.type) result.returnType = nodeToJson(node.type);
      if (node.body) result.body = nodeToJson(node.body);
      // Get the function's signature type
      try {
        const sig = checker.getSignatureFromDeclaration(node);
        if (sig) {
          result._returnType = checker.typeToString(sig.getReturnType());
          result._paramTypes = sig.getParameters().map(p =>
            checker.typeToString(checker.getTypeOfSymbolAtLocation(p, node))
          );
        }
      } catch (e) {}
      break;

    case ts.SyntaxKind.Parameter:
      result.name = nodeToJson(node.name);
      if (node.type) result.typeAnnotation = nodeToJson(node.type);
      if (node.initializer) result.initializer = nodeToJson(node.initializer);
      if (node.dotDotDotToken) result.isRest = true;
      if (node.questionToken) result.isOptional = true;
      break;

    case ts.SyntaxKind.Block:
      result.statements = node.statements.map(nodeToJson);
      break;

    case ts.SyntaxKind.ReturnStatement:
      if (node.expression) result.expression = nodeToJson(node.expression);
      break;

    case ts.SyntaxKind.ExpressionStatement:
      result.expression = nodeToJson(node.expression);
      break;

    case ts.SyntaxKind.BinaryExpression:
      result.left = nodeToJson(node.left);
      result.operator = ts.SyntaxKind[node.operatorToken.kind];
      result.right = nodeToJson(node.right);
      break;

    case ts.SyntaxKind.PrefixUnaryExpression:
      result.operator = ts.SyntaxKind[node.operator];
      result.operand = nodeToJson(node.operand);
      break;

    case ts.SyntaxKind.PostfixUnaryExpression:
      result.operator = ts.SyntaxKind[node.operator];
      result.operand = nodeToJson(node.operand);
      break;

    case ts.SyntaxKind.CallExpression:
      result.expression = nodeToJson(node.expression);
      result.arguments = node.arguments.map(nodeToJson);
      break;

    case ts.SyntaxKind.PropertyAccessExpression:
      result.expression = nodeToJson(node.expression);
      result.name = nodeToJson(node.name);
      break;

    case ts.SyntaxKind.ElementAccessExpression:
      result.expression = nodeToJson(node.expression);
      result.argumentExpression = nodeToJson(node.argumentExpression);
      break;

    case ts.SyntaxKind.IfStatement:
      result.expression = nodeToJson(node.expression);
      result.thenStatement = nodeToJson(node.thenStatement);
      if (node.elseStatement) result.elseStatement = nodeToJson(node.elseStatement);
      break;

    case ts.SyntaxKind.WhileStatement:
      result.expression = nodeToJson(node.expression);
      result.statement = nodeToJson(node.statement);
      break;

    case ts.SyntaxKind.ForStatement:
      if (node.initializer) result.initializer = nodeToJson(node.initializer);
      if (node.condition) result.condition = nodeToJson(node.condition);
      if (node.incrementor) result.incrementor = nodeToJson(node.incrementor);
      result.statement = nodeToJson(node.statement);
      break;

    case ts.SyntaxKind.ForOfStatement:
      result.initializer = nodeToJson(node.initializer);
      result.expression = nodeToJson(node.expression);
      result.statement = nodeToJson(node.statement);
      break;

    case ts.SyntaxKind.ArrayLiteralExpression:
      result.elements = node.elements.map(nodeToJson);
      break;

    case ts.SyntaxKind.ObjectLiteralExpression:
      result.properties = node.properties.map(nodeToJson);
      break;

    case ts.SyntaxKind.PropertyAssignment:
      result.name = nodeToJson(node.name);
      result.initializer = nodeToJson(node.initializer);
      break;

    case ts.SyntaxKind.ConditionalExpression:
      result.condition = nodeToJson(node.condition);
      result.whenTrue = nodeToJson(node.whenTrue);
      result.whenFalse = nodeToJson(node.whenFalse);
      break;

    case ts.SyntaxKind.TypeAssertionExpression:
    case ts.SyntaxKind.AsExpression:
      result.expression = nodeToJson(node.expression);
      result.typeAnnotation = nodeToJson(node.type);
      break;

    case ts.SyntaxKind.ClassDeclaration:
      if (node.name) result.name = nodeToJson(node.name);
      result.members = node.members.map(nodeToJson);
      if (node.heritageClauses) result.heritage = node.heritageClauses.map(nodeToJson);
      break;

    case ts.SyntaxKind.PropertyDeclaration:
      result.name = nodeToJson(node.name);
      if (node.type) result.typeAnnotation = nodeToJson(node.type);
      if (node.initializer) result.initializer = nodeToJson(node.initializer);
      break;

    case ts.SyntaxKind.Constructor:
      result.parameters = node.parameters.map(nodeToJson);
      if (node.body) result.body = nodeToJson(node.body);
      break;

    case ts.SyntaxKind.NewExpression:
      result.expression = nodeToJson(node.expression);
      result.arguments = node.arguments ? node.arguments.map(nodeToJson) : [];
      break;

    case ts.SyntaxKind.ThrowStatement:
      result.expression = nodeToJson(node.expression);
      break;

    case ts.SyntaxKind.TryStatement:
      result.tryBlock = nodeToJson(node.tryBlock);
      if (node.catchClause) result.catchClause = nodeToJson(node.catchClause);
      if (node.finallyBlock) result.finallyBlock = nodeToJson(node.finallyBlock);
      break;

    case ts.SyntaxKind.CatchClause:
      if (node.variableDeclaration) result.variableDeclaration = nodeToJson(node.variableDeclaration);
      result.block = nodeToJson(node.block);
      break;

    case ts.SyntaxKind.TypeReference:
      result.typeName = nodeToJson(node.typeName);
      if (node.typeArguments) result.typeArguments = node.typeArguments.map(nodeToJson);
      break;

    case ts.SyntaxKind.NumberKeyword:
    case ts.SyntaxKind.StringKeyword:
    case ts.SyntaxKind.BooleanKeyword:
    case ts.SyntaxKind.VoidKeyword:
    case ts.SyntaxKind.UndefinedKeyword:
    case ts.SyntaxKind.NeverKeyword:
    case ts.SyntaxKind.AnyKeyword:
    case ts.SyntaxKind.UnknownKeyword:
      // Type keywords — _kind is sufficient
      break;

    case ts.SyntaxKind.ArrayType:
      result.elementType = nodeToJson(node.elementType);
      break;

    case ts.SyntaxKind.InterfaceDeclaration:
      result.name = nodeToJson(node.name);
      result.members = node.members.map(nodeToJson);
      break;

    case ts.SyntaxKind.PropertySignature:
      result.name = nodeToJson(node.name);
      if (node.type) result.typeAnnotation = nodeToJson(node.type);
      if (node.questionToken) result.isOptional = true;
      break;

    case ts.SyntaxKind.TemplateExpression:
      result.head = nodeToJson(node.head);
      result.templateSpans = node.templateSpans.map(nodeToJson);
      break;

    case ts.SyntaxKind.TemplateHead:
    case ts.SyntaxKind.TemplateMiddle:
    case ts.SyntaxKind.TemplateTail:
      result.text = node.text;
      break;

    case ts.SyntaxKind.TemplateSpan:
      result.expression = nodeToJson(node.expression);
      result.literal = nodeToJson(node.literal);
      break;

    case ts.SyntaxKind.BreakStatement:
    case ts.SyntaxKind.ContinueStatement:
      break;

    case ts.SyntaxKind.SwitchStatement:
      result.expression = nodeToJson(node.expression);
      result.caseBlock = nodeToJson(node.caseBlock);
      break;

    case ts.SyntaxKind.CaseBlock:
      result.clauses = node.clauses.map(nodeToJson);
      break;

    case ts.SyntaxKind.CaseClause:
      result.expression = nodeToJson(node.expression);
      result.statements = node.statements.map(nodeToJson);
      break;

    case ts.SyntaxKind.DefaultClause:
      result.statements = node.statements.map(nodeToJson);
      break;

    default:
      // For unhandled nodes, recurse into children
      ts.forEachChild(node, child => {
        const key = "_children";
        if (!result[key]) result[key] = [];
        result[key].push(nodeToJson(child));
      });
      break;
  }

  return result;
}

const ast = nodeToJson(sourceFile);
fs.writeFileSync(outputFile, JSON.stringify(ast, null, 2));
