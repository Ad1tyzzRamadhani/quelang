std::unique_ptr<Literal> ParseState::parseLiteral(const Token& tok) {
    auto lit = std::make_unique<Literal>();
    lit->line_start = tok.line;
    lit->column_start = tok.column;
    lit->line_end = tok.line;
    lit->column_end = tok.column - 1;

    switch(tok.type) {
        case TokenType::NUMBER:
            lit->kind = Literal::Kind::Number;
            lit->value = tok.value;
            break;
        case TokenType::FLOAT:
            lit->kind = Literal::Kind::Float;
            lit->value = tok.value;
            break;
        case TokenType::HEXNUMBER:
            lit->kind = Literal::Kind::Hex;
            lit->value = tok.value;
            break;
        case TokenType::BINARYNUMBER:
            lit->kind = Literal::Kind::Binary;
            lit->value = tok.value;
            break;
        case TokenType::STRING:
            lit->kind = Literal::Kind::String;
            lit->value = tok.value;
            break;
        case TokenType::RAW_STRING:
            lit->kind = Literal::Kind::RawString;
            lit->value = tok.value;
            break;
        case TokenType::CHAR_LITERAL:
            lit->kind = Literal::Kind::Char;
            lit->value = tok.value;
            break;
        case TokenType::KW_TRUE:
            lit->kind = Literal::Kind::True;
            lit->value = "true";
            break;
        case TokenType::KW_NONE:
            lit->kind = Literal::Kind::None;
            lit->value = "none";
            break;
        case TokenType::KW_FALSE:
            lit->kind = Literal::Kind::False;
            lit->value = "false";
            break;
        case TokenType::KW_NULL:
            lit->kind = Literal::Kind::Null;
            lit->value = "null";
            break;
        default:
            error("unexpected token type while parsing literal value");
    }

    return lit;
}

std::unique_ptr<QualifiedName> ParseState::parseQualifiedName() {
    auto qn = std::make_unique<QualifiedName>();
    if (match(TokenType::SCOPE)) {
        qn->is_absolute = true;
    }

    if (!tokens.empty() && pos < tokens.size()) {
        qn->line_start = tokens[pos].line;
        qn->column_start = tokens[pos].column;
    }

    while(pos < tokens.size()) {
        if(tokens[pos].type != TokenType::IDENT)
            break;

        qn->parts.push_back(tokens[pos].value);
        pos++;

        if(pos < tokens.size() && tokens[pos].type == TokenType::SCOPE) {
            pos++;
        } else {
            break;
        }
    }

    if(qn->parts.empty())
        error("expected identifier to form a qualified name");

    return qn;
}

std::unique_ptr<QualifiedName> ParseState::MakeEmptyQualifiedName() {
    auto qn = std::make_unique<QualifiedName>();
    qn->line_start = tokens[pos].line;
    qn->column_start = tokens[pos].column;
    qn->parts.push_back("funcptr");

    return qn;
}

bool ParseState::isStructInit() {
    size_t save = pos;
    try {
        // Coba parse sebagai QualifiedName (misal: Point, pkg.Point, dll)
        parseType();
        
        // Cek apakah setelah QualifiedName terdapat token LBRACE '{'
        bool hasLBrace = check(TokenType::LBRACE);
        
        // Kembalikan posisi semula (rollback)
        pos = save;
        return hasLBrace;
    } catch (...) {
        pos = save;
        return false;
    }
}


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
        tok.type == TokenType::KW_NONE ||
        tok.type == TokenType::KW_NULL)
    {
        advance();

        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Literal;
        expr->file = tok.file;
        expr->line_start = tok.line;
        expr->column_start = tok.column;
        expr->literal = parseLiteral(tok);

        return expr;
    }

    // ------------------------
    // STRUCT INIT { ... }
    // ------------------------
    if (isStructInit()) {
        const Token& structTok = peek(); // Simpan token awal struct init
        auto expr = std::make_unique<Expr>();
        expr->file = structTok.file;
        expr->line_start = structTok.line;
        expr->column_start = structTok.column;
        expr->struct_init.type_name = parseType();
        expr->kind = Expr::Kind::StructInit;

        consume(TokenType::LBRACE, "expected '{' to begin struct initialization block");

        if (!check(TokenType::RBRACE)) {
            do {
                // field = expr OR expr
                if (peek().type == TokenType::IDENT &&
                    peek(1).type == TokenType::EQ) 
                {
                    std::string name = advance().value;
                    consume(TokenType::EQ, "expected '=' after field name in struct initialization");

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

        consume(TokenType::RBRACE, "expected '}' to close struct initialization block");

        return expr;
    }

    // ------------------------
    // IDENT / QUALIFIED NAME
    // ------------------------
    if (tok.type == TokenType::IDENT || tok.type == TokenType::SCOPE) {
        size_t save = pos;

        try {
            auto qn = parseQualifiedName();

            auto expr = std::make_unique<Expr>();
            expr->file = tok.file;
            expr->line_start = tok.line;
            expr->column_start = tok.column;
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
        const Token& parenTok = tokens[pos - 1]; // Token '(' yang di-match
        auto expr = parseExpr(); 
        if (expr) {
            expr->file = parenTok.file;
            expr->line_start = parenTok.line;
            expr->column_start = parenTok.column;
        }

        consume(TokenType::RPAREN, "expected ')' to close grouped expression");

        return expr;
    }

    // ------------------------
    // ARRAY LITERAL [ ... ]
    // ------------------------
    if (match(TokenType::LBRACKET)) {
        const Token& bracketTok = tokens[pos - 1]; // Token '[' yang di-match
        auto expr = std::make_unique<Expr>();
        expr->file = bracketTok.file;
        expr->line_start = bracketTok.line;
        expr->column_start = bracketTok.column;
        expr->kind = Expr::Kind::ArrayLiteral;

        if (!check(TokenType::RBRACKET)) {
            do {
                expr->array_items.push_back(parseExpr());
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RBRACKET, "expected ']' to close array literal");

        return expr;
    }

    // ------------------------
    // NEW expression
    // new Type(...) & new Type[...]
    // ------------------------
    if (match(TokenType::KW_NEW)) {
        const Token& newTok = tokens[pos - 1]; // Token 'new' yang di-match
        auto expr = std::make_unique<Expr>();
        expr->file = newTok.file;
        expr->line_start = newTok.line;
        expr->column_start = newTok.column;
        expr->kind = Expr::Kind::New;
        if (match(TokenType::LPAREN)) {
            if(check(TokenType::NUMBER) || check(TokenType::HEXNUMBER) || check(TokenType::BINARYNUMBER)) {
                expr->new_expr.alloc_size = parseLiteral(advance());
            }
            consume(TokenType::RPAREN, "expected ')' after allocation size in 'new' expression");
            return expr;
        }

        expr->new_expr.type = parseQualifiedName();
        if (match(TokenType::LPAREN)) {
            if (!check(TokenType::RPAREN)) {
                do {
                    expr->new_expr.args.push_back(parseExpr());
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN, "expected ')' after constructor arguments in 'new' expression");
        }
        while (match(TokenType::LBRACKET)) {
            if (!check(TokenType::RBRACKET)) {
                expr->new_expr.array_dims.push_back(parseExpr());
            } else error("array dimension inside 'new' expression cannot be empty");
            consume(TokenType::RBRACKET,"expected ']' to close array dimension in 'new' expression");
        }

        return expr;
    }

    // ------------------------
    // THIS
    // ------------------------

    if (match(TokenType::KW_THIS)) {
        const Token& thisTok = tokens[pos - 1]; // Token 'this' yang di-match
        auto expr = std::make_unique<Expr>();
        expr->file = thisTok.file;
        expr->line_start = thisTok.line;
        expr->column_start = thisTok.column;
        expr->kind = Expr::Kind::Ident;

        auto qn = std::make_unique<QualifiedName>();
        qn->parts.push_back("this");

        expr->ident = std::move(qn);
        return expr;
    }

    if (match(TokenType::ARROW)) {
        const Token& arrowTok = tokens[pos - 1];
        if (peek().type != TokenType::IDENT)
            error("expected identifier after '->' operator");

        auto thisExpr = std::make_unique<Expr>();
        thisExpr->kind = Expr::Kind::Ident;

        auto qn = std::make_unique<QualifiedName>();
        qn->parts.push_back("this");
        thisExpr->ident = std::move(qn);

        Expr::PostfixOp op;
        op.kind = Expr::PostfixOp::Kind::Arrow;
        op.name = advance().value;

        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Postfix;
        
        expr->postfix.base = std::move(thisExpr);
        expr->postfix.ops.push_back(std::move(op));

        expr->file = arrowTok.file;
        expr->line_start = arrowTok.line;
        expr->column_start = arrowTok.column;

        return expr;
    }

    error("invalid or unexpected primary expression");
    return nullptr;
}

std::unique_ptr<Expr> ParseState::parsePostfix() {
    auto base = parsePrimary();

    while (true) {

        if (match(TokenType::DOT)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '.' member access operator");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::MemberAccess;
            op.name = advance().value;

            /*auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));*/

            //base = std::move(expr);
            appendPostfix(base, std::move(op));
            continue;
        }

        if (match(TokenType::ARROW)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '->' arrow operator");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Arrow;
            op.name = advance().value;

            /*auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));*/

            //base = std::move(expr);
            appendPostfix(base, std::move(op));
            continue;
        }

        if (match(TokenType::SAFE_ACCESS)) {
            if (peek().type != TokenType::IDENT)
                error("expected identifier after '?->' safe access operator");

            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::SafeArrow;
            op.name = advance().value;

            /*auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));*/

            //base = std::move(expr);
            appendPostfix(base, std::move(op));
            continue;
        }
        

        if (match(TokenType::LPAREN)) {
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Call;
            op.args = parseArgList();

            consume(TokenType::RPAREN, "expected ')' to close function call argument list");

            /*auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));*/

            //base = std::move(expr);
            appendPostfix(base, std::move(op));
            continue;
        }

        if (match(TokenType::LBRACKET)) {
            Expr::PostfixOp op;
            op.kind = Expr::PostfixOp::Kind::Index;
            op.index = parseExpr();

            consume(TokenType::RBRACKET, "expected ']' to close array indexing expression");

            /*auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Postfix;
            expr->postfix.base = std::move(base);
            expr->postfix.ops.push_back(std::move(op));*/

            //base = std::move(expr);
            appendPostfix(base, std::move(op));
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
        if (castExpr->cast.base) {
            castExpr->file = castExpr->cast.base->file;
            castExpr->line_start = castExpr->cast.base->line_start;
            castExpr->column_start = castExpr->cast.base->column_start;
        }
        if(match(TokenType::COLON)) {
            if (!check(TokenType::NUMBER) && !check(TokenType::HEXNUMBER)) {
                error("expected numeric literal for bit width specification after cast colon");
            }
            castExpr->cast.target->bit_width = parseLiteral(advance());
        }
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
        match(TokenType::KW_MOVE) ||
        match(TokenType::KW_NOT) ||
        match(TokenType::BANG))
    {
        TokenType opTok = tokens[pos - 1].type;

        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Unary;
        expr->file = tokens[pos - 1].file;
        expr->line_start = tokens[pos - 1].line;
        expr->column_start = tokens[pos - 1].column;

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
            case TokenType::BANG:
            case TokenType::KW_NOT:
                expr->unary.op = UnaryOp::Not;
                break;

            case TokenType::KW_MOVE:
                expr->unary.op = UnaryOp::Move;
                break;

            default:
                error("invalid unary operator encountered");
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
                error("invalid multiplicative binary operator");
        }

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
                error("invalid additive binary operator");
        }

        bin->binary.lhs = std::move(expr);
        bin->binary.rhs = std::move(rhs);
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
            if (bin->binary.lhs) {
                bin->file = bin->binary.lhs->file;
                bin->line_start = bin->binary.lhs->line_start;
                bin->column_start = bin->binary.lhs->column_start;
            }

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
            if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
            }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

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
            if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
            }

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
            if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
            }

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
            if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
            }

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
        if (bin->binary.lhs) {
            bin->file = bin->binary.lhs->file;
            bin->line_start = bin->binary.lhs->line_start;
            bin->column_start = bin->binary.lhs->column_start;
        }

        expr = std::move(bin);
    }

    return expr;
}

// Ternary Expr Parser

std::unique_ptr<Expr> ParseState::parseTernary() {

    auto expr = parseCoalesce();

    if (match(TokenType::QUESTION)) {

        auto thenExpr = parseExpr();

        consume(TokenType::COLON, "expected ':' separating branches in ternary conditional expression");

        auto elseExpr = parseTernary();

        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Ternary;

        // Ternary Move / init
        node->ternary.cond = std::move(expr);
        node->ternary.then_expr = std::move(thenExpr);
        node->ternary.else_expr = std::move(elseExpr);
        if (node->ternary.cond) {
            node->file = node->ternary.cond->file;
            node->line_start = node->ternary.cond->line_start;
            node->column_start = node->ternary.cond->column_start;
        }

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
        if (node->assign.lhs) {
        node->file = node->assign.lhs->file;
        node->line_start = node->assign.lhs->line_start;
        node->column_start = node->assign.lhs->column_start;
        }

        expr = std::move(node);
    }

    return expr;
}
