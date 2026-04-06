
std::unique_ptr<Expr> parsePostfix(ParseState& state, std::unique_ptr<Expr> base) {
    auto expr = std::make_unique<Expr>();
    expr->kind = Expr::Kind::Postfix;
    expr->postfix.base = std::move(base);

    while (true) {
        Token tok = state.peek();

        if (tok.type == TokenType::DOT || tok.type == TokenType::SAFE_DOT) {
            state.advance();
            if (!state.match(TokenType::IDENT))
                state.error("Expected identifier after '.' or '?.'");
            Expr::PostfixOp op;
            op.kind = (tok.type == TokenType::DOT ? Expr::PostfixOp::Kind::Field
                                                  : Expr::PostfixOp::Kind::NullField);
            op.name = state.peek(-1).value;
            expr->postfix.ops.push_back(std::move(op));
        } 
        else if (tok.type == TokenType::SCOPE) { // ::
            state.advance();
            if (!state.match(TokenType::IDENT))
                state.error("Expected identifier after '::'");
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Scope;
            op.name = state.peek(-1).value;
            expr->postfix.ops.push_back(std::move(op));
        } 
        else if (tok.type == TokenType::ARROW) { // ->
            state.advance();
            if (!state.match(TokenType::IDENT))
                state.error("Expected identifier after '->'");
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Arrow;
            op.name = state.peek(-1).value;
            expr->postfix.ops.push_back(std::move(op));
        } 
        else if (tok.type == TokenType::LPAREN) { // function call
            state.advance();
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Call;
            while (!state.match(TokenType::RPAREN)) {
                op.args.push_back(parseExpr(state));
                state.match(TokenType::COMMA);
            }
            expr->postfix.ops.push_back(std::move(op));
        } 
        else if (tok.type == TokenType::LBRACKET) {
            state.advance();
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Index;
            op.index = parseExpr(state);
            if (!state.match(TokenType::RBRACKET))
                state.error("Expected ']' after index expression");
            expr->postfix.ops.push_back(std::move(op));
        } 
        else break;
    }

    return expr;
}
