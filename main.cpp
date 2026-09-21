#include "directivelinker.cpp"
#include "lexer.cpp"
#include "ast.hpp"
#include "parser.cpp"
#include "parser_literal.cpp"
#include "parser_type.cpp"
#include "parser_expr.cpp"
#include "parser_stmt.cpp"
#include "parser_toplevel.cpp"
#include "semantic_symbol.cpp"
#include "semantic.hpp"
#include "semantic.cpp"

int main() {
  std::string rawprogram = preprocess("main.q");
  Lexer lex(rawprogram);
  std::vector<Token> token = lex.tokenize();
  ParseState parser{token};
  SemanticAnalyzer semantic;
  semantic.analyze(parser.parseProgram());
  auto diagnostics = semantic.diagnostics();
  for(auto diagnostic : diagnostics) {
    if(diagnostic.is_warning) {
      std::cout << diagnostic.file + " : " + diagnostic.message + diagnostic.line + diagnostic.collumn;
      continue;
    }
    std::cerr << diagnostic.file + " : " + diagnostic.message + diagnostic.line + diagnostic.collumn;
  }
  return 0;
}
