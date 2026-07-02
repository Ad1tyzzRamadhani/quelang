std::unique_ptr<Expr> ParseState::parsePrimary() {
    const Token& tok = peek();

    // ------------------------
    // LITERAL
    // ------------------------
    if (tok.type == TokenType::NUMBER ||
        tok.type == TokenType::FLOAT ||
        tok.type == TokenType::HEXNUMBER ||
        tok.type == TokenType::BINARYNUMBER ||
        tok.type == TokenType::STRING ||
        tok.type == TokenType::RAW_STRING ||
        tok.type == TokenType::CHAR_LITERAL ||
        tok.type == TokenType::KW_TRUE ||
        tok.type == TokenType::KW_FALSE ||
        tok.type == TokenType::KW_NULL) 
    {
        advance();

        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Literal;
        expr->literal = parseLiteral(tok);

        return expr;
    }

    // ------------------------
    // IDENT / QUALIFIED NAME
    // ------------------------
    if (tok.type == TokenType::IDENT) {
        size_t save = pos;

        try {
            auto qn = parseQualifiedName(tokens, pos);

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Ident;
            expr->ident = std::move(qn);

            return expr;
        }
        catch (...) {
            pos = save;
        }
    }

    // ------------------------
    // GROUPING (expr)
    // ------------------------
    if (match(TokenType::LPAREN)) {
        auto expr = parseExpr(); // nanti precedence parser

        consume(TokenType::RPAREN, "expected ')'");

        return expr;
    }

    // ------------------------
    // ARRAY LITERAL [ ... ]
    // ------------------------
    if (match(TokenType::LBRACKET)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::ArrayLiteral;

        if (!check(TokenType::RBRACKET)) {
            do {
                expr->array_items.push_back(parseExpr());
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RBRACKET, "expected ']'");

        return expr;
    }

    // ------------------------
    // STRUCT INIT { ... }
    // ------------------------
    if (match(TokenType::LBRACE)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::StructInit;

        if (!check(TokenType::RBRACE)) {
            do {
                // field = expr OR expr
                if (peek().type == TokenType::IDENT &&
                    peek(1).type == TokenType::EQ) 
                {
                    std::string name = advance().value;
                    consume(TokenType::EQ);

                    expr->struct_init.fields.push_back({
                        name,
                        parseExpr()
                    });
                } else {
                    expr->struct_init.fields.push_back({
                        std::nullopt,
                        parseExpr()
                    });
                }

            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RBRACE, "expected '}'");

        return expr;
    }

    // ------------------------
    // NEW expression
    // new Type(...)
    // ------------------------
    if (match(TokenType::KW_NEW)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::New;

        expr->new_expr.type = parseQualifiedName(tokens, pos);

        if (match(TokenType::LPAREN)) {
            if (!check(TokenType::RPAREN)) {
                do {
                    expr->new_expr.args.push_back(parseExpr());
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN);
        }
        if (match(TokenType::LBRACKET)) {
            if (!check(TokenType::RBRACKET)) {
                expr->new_expr.array_dims;

        return expr;
    }

    // ------------------------
    // THIS
    // ------------------------
    if (match(TokenType::KW_THIS)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Ident;

        auto qn = std::make_unique<QualifiedName>();
        qn->parts.push_back("this");

        expr->ident = std::move(qn);
        return expr;
    }

    if (match(TokenType::KW_DEFAULT)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Ident;

        auto qn = std::make_unique<QualifiedName>();
        qn->parts.push_back("default");

        expr->ident = std::move(qn);
        return expr;
    }

    error("invalid primary expression");
    return nullptr;
}

std::unique_ptr<Expr> ParseState::parsePostfix() {
    auto base = parsePrimary();

    while (true) {

        if (match(TokenType::DOT)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '.'");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::MemberAccess;
            op.name = advance().value;

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }

        if (match(TokenType::ARROW)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '->'");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Arrow;
            op.name = advance().value;

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }

        if (match(TokenType::SAFE_ACCESS)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '?->'");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::SafeArrow;
            op.name = advance().value;

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }
        

        if (match(TokenType::LPAREN)) {
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Call;
            op.args = parseArgList();

            consume(TokenType::RPAREN);

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }

        if (match(TokenType::LBRACKET)) {
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Index;
            op.index = parseExpr();

            consume(TokenType::RBRACKET);

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }

        if (match(TokenType::KW_UNSAFE)) {
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Unsafe;

            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));

            base = std::move(expr);
            continue;
        }

        break;
    }

    return base;
}
