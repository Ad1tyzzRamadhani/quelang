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
    // new Type(...) & new Type[...]
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
        while (match(TokenType::LBRACKET)) {
            if (!check(TokenType::RBRACKET)) {
                expr->new_expr.array_dims.push_back(parseExpr());
            } else error("Array Dims in '[ ... ]' Can't Empty!");
            consume(TokenType::RBRACKET);
        }

        return expr;
    }

    // ------------------------
    // THIS
    // ------------------------
    if (match(TokenType::KW_MOVE)) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Move;
        expr->move_expr.value = parseExpr();
        return expr;
    }

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

std::unique_ptr<Expr> ParseState::parseCast() {
    auto expr = parsePostfix();

    while (match(TokenType::KW_AS)) {

        auto castExpr = std::make_unique<Expr>();
        castExpr->kind = Expr::Kind::Cast;

        castExpr->cast.base = std::move(expr);
        castExpr->cast.target = parseType();

        expr = std::move(castExpr);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseUnary() {

    // -------------------------
    // Unary operators
    // -, *, &, ~, not
    // -------------------------
    if (match(TokenType::MINUS) ||
        match(TokenType::STAR)  ||
        match(TokenType::AMP)   ||
        match(TokenType::TILDE) ||
        match(TokenType::KW_NOT)) 
    {
        TokenType opTok = tokens[pos - 1].type;

        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Unary;

        switch (opTok) {
            case TokenType::MINUS:
                expr->unary.op = UnaryOp::Neg;
                break;

            case TokenType::STAR:
                expr->unary.op = UnaryOp::Deref;
                break;

            case TokenType::AMP:
                expr->unary.op = UnaryOp::Ref;
                break;

            case TokenType::TILDE:
                expr->unary.op = UnaryOp::BitNot;
                break;

            case TokenType::KW_NOT:
                expr->unary.op = UnaryOp::Not;
                break;

            default:
                error("invalid unary operator");
        }

        expr->unary.expr = parseUnary(); // recursive right-associative
        return expr;
    }

    // -------------------------
    // fallback → CAST level
    // -------------------------
    return parseCast();
}

std::unique_ptr<Expr> ParseState::parseMul() {

    auto expr = parseUnary();

    while (true) {

        TokenType op;

        if (match(TokenType::STAR)) op = TokenType::STAR;
        else if (match(TokenType::SLASH)) op = TokenType::SLASH;
        else if (match(TokenType::PERCENT)) op = TokenType::PERCENT;
        else break;

        auto rhs = parseUnary();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;

        switch (op) {
            case TokenType::STAR:
                bin->binary.op = BinaryOp::Mul;
                break;

            case TokenType::SLASH:
                bin->binary.op = BinaryOp::Div;
                break;

            case TokenType::PERCENT:
                bin->binary.op = BinaryOp::Mod;
                break;

            default:
                error("invalid mul operator");
        }

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseAdd() {

    auto expr = parseMul();

    while (true) {

        TokenType op;

        if (match(TokenType::PLUS)) op = TokenType::PLUS;
        else if (match(TokenType::MINUS)) op = TokenType::MINUS;
        else break;

        auto rhs = parseMul();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;

        switch (op) {
            case TokenType::PLUS:
                bin->binary.op = BinaryOp::Add;
                break;

            case TokenType::MINUS:
                bin->binary.op = BinaryOp::Sub;
                break;

            default:
                error("invalid add operator");
        }

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseBitwiseAnd() {

    auto expr = parseAdd();

    while (true) {

        if (!match(TokenType::AMP))
            break;

        auto rhs = parseAdd();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = BinaryOp::BitAnd;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseBitwiseXor() {

    auto expr = parseBitwiseAnd();

    while (true) {

        if (!match(TokenType::CARET))
            break;

        auto rhs = parseBitwiseAnd();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = BinaryOp::BitXor;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseBitwiseOr() {

    auto expr = parseBitwiseXor();

    while (true) {

        if (!match(TokenType::BAR))
            break;

        auto rhs = parseBitwiseXor();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = BinaryOp::BitOr;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseShift() {

    auto expr = parseBitwiseOr();

    while (true) {

        if (match(TokenType::LSHIFT)) {

            auto rhs = parseBitwiseOr();

            auto bin = std::make_unique<Expr>();
            bin->kind = Expr::Kind::Binary;
            bin->binary.op = BinaryOp::Shl;

            bin->binary.lhs = std::move(expr);
            bin->binary.rhs = std::move(rhs);

            expr = std::move(bin);
            continue;
        }

        if (match(TokenType::RSHIFT)) {

            auto rhs = parseBitwiseOr();

            auto bin = std::make_unique<Expr>();
            bin->kind = Expr::Kind::Binary;
            bin->binary.op = BinaryOp::Shr;

            bin->binary.lhs = std::move(expr);
            bin->binary.rhs = std::move(rhs);

            expr = std::move(bin);
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseRelational() {

    auto expr = parseShift();

    while (true) {

        BinaryOp op;
        bool ok = true;

        if (match(TokenType::LT)) op = BinaryOp::Lt;
        else if (match(TokenType::LTE)) op = BinaryOp::Lte;
        else if (match(TokenType::GT)) op = BinaryOp::Gt;
        else if (match(TokenType::GTE)) op = BinaryOp::Gte;
        else ok = false;

        if (!ok) break;

        auto rhs = parseShift();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = op;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseEquality() {

    auto expr = parseRelational();

    while (true) {

        BinaryOp op;
        bool ok = true;

        if (match(TokenType::EQEQ)) op = BinaryOp::Eq;
        else if (match(TokenType::NEQ)) op = BinaryOp::Ne;
        else ok = false;

        if (!ok) break;

        auto rhs = parseRelational();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = op;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseLogic() {

    auto expr = parseEquality();

    while (true) {

        if (match(TokenType::KW_AND)) {

            auto rhs = parseEquality();

            auto bin = std::make_unique<Expr>();
            bin->kind = Expr::Kind::Binary;
            bin->binary.op = BinaryOp::And;

            bin->binary.lhs = std::move(expr);
            bin->binary.rhs = std::move(rhs);

            expr = std::move(bin);
            continue;
        }

        if (match(TokenType::KW_OR)) {

            auto rhs = parseEquality();

            auto bin = std::make_unique<Expr>();
            bin->kind = Expr::Kind::Binary;
            bin->binary.op = BinaryOp::Or;

            bin->binary.lhs = std::move(expr);
            bin->binary.rhs = std::move(rhs);

            expr = std::move(bin);
            continue;
        }

        if (match(TokenType::KW_XOR)) {

            auto rhs = parseEquality();

            auto bin = std::make_unique<Expr>();
            bin->kind = Expr::Kind::Binary;
            bin->binary.op = BinaryOp::Xor;

            bin->binary.lhs = std::move(expr);
            bin->binary.rhs = std::move(rhs);

            expr = std::move(bin);
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseCoalesce() {

    auto expr = parseLogic();

    while (true) {

        if (!match(TokenType::COALESCE))
            break;

        auto rhs = parseLogic();

        auto bin = std::make_unique<Expr>();
        bin->kind = Expr::Kind::Binary;
        bin->binary.op = BinaryOp::Coalesce;

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);

        expr = std::move(bin);
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseTernary() {

    auto expr = parseCoalesce();

    if (match(TokenType::QUESTION)) {

        auto thenExpr = parseExpr();

        consume(TokenType::COLON, "expected ':' in ternary");

        auto elseExpr = parseTernary();

        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Ternary;

        node->ternary.cond = std::move(expr);
        node->ternary.then_expr = std::move(thenExpr);
        node->ternary.else_expr = std::move(elseExpr);

        return node;
    }

    return expr;
}

std::unique_ptr<Expr> ParseState::parseAssign() {

    auto expr = parseTernary();

    while (true) {

        AssignOp op;
        bool ok = true;

        if (match(TokenType::EQ)) op = AssignOp::Assign;
        else if (match(TokenType::PLUS_EQ)) op = AssignOp::AddEq;
        else if (match(TokenType::MINUS_EQ)) op = AssignOp::SubEq;
        else if (match(TokenType::STAR_EQ)) op = AssignOp::MulEq;
        else if (match(TokenType::SLASH_EQ)) op = AssignOp::DivEq;
        else if (match(TokenType::PERCENT_EQ)) op = AssignOp::ModEq;
        else ok = false;

        if (!ok) break;

        auto rhs = parseAssign(); // right associative

        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Assign;

        node->assign.op = op;
        node->assign.lhs = std::move(expr);
        node->assign.rhs = std::move(rhs);

        expr = std::move(node);
    }

    return expr;
}
