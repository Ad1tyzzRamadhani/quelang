std::unique_ptr<Stmt> ParseState::parseStmt() {

    const Token& tok = peek();

    // -------------------------
    // BLOCK
    // -------------------------
    if (tok.type == TokenType::LBRACE) {
        return parseBlock(); // sudah dipisah sesuai grammar
    }

    // -------------------------
    // IF
    // -------------------------
    if (match(TokenType::KW_IF)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::If;

        consume(TokenType::LPAREN, "expected '(' after if");

        stmt->if_stmt.cond = parseExpr();

        consume(TokenType::RPAREN, "expected ')' after if condition");

        stmt->if_stmt.then_block = parseStmt();

        // elif chain
        while (match(TokenType::KW_ELIF)) {
            consume(TokenType::LPAREN);
            auto cond = parseExpr();
            consume(TokenType::RPAREN);
            Stmt::IfStmt::Elif e;
            e.cond = std::move(cond);
            e.block = parseStmt();

            stmt->if_stmt.elifs.push_back(std::move(e));
        }

        // else
        if (match(TokenType::KW_ELSE)) {
            stmt->if_stmt.else_block = parseStmt();
        }

        return stmt;
    }

    // -------------------------
    // WHILE
    // -------------------------
    if (match(TokenType::KW_WHILE)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::While;

        consume(TokenType::LPAREN);
        stmt->while_stmt.cond = parseExpr();
        consume(TokenType::RPAREN);

        stmt->while_stmt.body = parseStmt();

        return stmt;
    }

    if (match(TokenType::KW_SWITCH))
        return parseSwitchStmt();

    if (match(TokenType::KW_USE)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::UseStmt;
        stmt->use_stmt = parseUseDecl();
        return stmt;
    }
    // -------------------------
    // DO WHILE
    // -------------------------
    if (match(TokenType::KW_DO)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::DoWhile;

        stmt->do_while_stmt.body = parseStmt();

        consume(TokenType::KW_WHILE);
        consume(TokenType::LPAREN);
        stmt->do_while_stmt.cond = parseExpr();
        consume(TokenType::RPAREN);
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // FOR
    // -------------------------
    if (match(TokenType::KW_FOR)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::For;

        consume(TokenType::LPAREN);

        stmt->for_stmt.init = parseStmt();

        consume(TokenType::SEMICOLON);

        if (!check(TokenType::SEMICOLON))
            stmt->for_stmt.cond = parseExpr();

        consume(TokenType::SEMICOLON);

        if (!check(TokenType::RPAREN))
            stmt->for_stmt.update = parseExpr();

        consume(TokenType::RPAREN);

        stmt->for_stmt.body = parseStmt();

        return stmt;
    }

    // -------------------------
    // RETURN
    // -------------------------
    if (match(TokenType::KW_RETURN)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Return;

        if (!check(TokenType::SEMICOLON))
            stmt->return_values = parseExpr();

        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // BREAK
    // -------------------------
    if (match(TokenType::KW_BREAK)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Break;

        consume(TokenType::SEMICOLON);
        return stmt;
    }

    // -------------------------
    // CONTINUE
    // -------------------------
    if (match(TokenType::KW_CONTINUE)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Continue;

        consume(TokenType::SEMICOLON);
        return stmt;
    }

    // -------------------------
    // DROP / WIPE
    // -------------------------
    if (match(TokenType::KW_DROP)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Drop;

        stmt->drop_stmt.expr = parseExpr();
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    if (match(TokenType::KW_WIPE)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Wipe;

        stmt->wipe_stmt.expr = parseExpr();
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // DEFER
    // -------------------------
    if (match(TokenType::KW_DEFER)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Defer;

        stmt->defer_stmt.stmt = parseStmt();

        return stmt;
    }

    // -------------------------
    // YIELD
    // -------------------------
    if (match(TokenType::KW_YIELD)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Yield;

        consume(TokenType::SEMICOLON);
        return stmt;
    }

    // -------------------------
    // RESUME
    // -------------------------
    if (match(TokenType::KW_RESUME)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Resume;

        stmt->resume_stmt.target = parseExpr();
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // LABEL
    // -------------------------
    if (tok.type == TokenType::IDENT && peek(1).type == TokenType::COLON) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Label;

        stmt->label = advance().value;
        consume(TokenType::COLON);

        return stmt;
    }

    // -------------------------
    // JUMPTO
    // -------------------------
    if (match(TokenType::KW_JUMPTO)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Jump;

        stmt->jump_target = advance().value;
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // VARIABLE DECL / EXPRESSIONS
    // -------------------------
    if (isTypeStart(tok)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::VarDecl;

        stmt->var_decl = parseVarDecl();
        consume(TokenType::SEMICOLON);

        return stmt;
    }

    // -------------------------
    // FALLBACK: ExprStmt
    // -------------------------
    {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::ExprStmt;

        stmt->expr_stmt = parseExpr();
        consume(TokenType::SEMICOLON);

        return stmt;
    }
}

Stmt::VarDecl ParseState::parseVarDecl() {
    Stmt::VarDecl decl;
    const Token& tok = peek();

    // -------------------------
    // StorageOpt
    // -------------------------
    if (match(TokenType::KW_STATIC))
        decl.is_static = true;

    else if (match(TokenType::KW_EXTERN))
        decl.is_extern = true;

    else
        decl.is_none = true;

    // -------------------------
    // Type
    // -------------------------
    decl.type = parseType();

    // -------------------------
    // VarList
    // -------------------------
    do {

        Stmt::VarDecl::Item item;

        if (peek().type != TokenType::IDENT)
            error("expected variable name");

        item.name = advance().value;

        // -------------------------
        // ArrayDimsOpt
        // -------------------------
        while (match(TokenType::LBRACKET)) {

            if (peek().type != TokenType::NUMBER)
                error("array dimension must be constant number");

            item.array_dims.push_back(
                parseLiteral(tok);
            );

            consume(TokenType::RBRACKET);
        }

        if (match(TokenType::LPAREN)) {
            if (peek().type != TokenType::RPAREN) {
                item.args = parseArgList();
            }
            consume(TokenType::RPAREN);
        }

        // -------------------------
        // VarInitOpt
        // -------------------------
        if (match(TokenType::EQ)) {
            item.init = parseExpr();
        }

        decl.items.push_back(std::move(item));

    } while (match(TokenType::COMMA));

    return decl;
}

std::unique_ptr<Stmt> ParseState::parseBlock() {

    consume(TokenType::LBRACE, "expected '{'");

    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::Block;

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN))
    {
        stmt->block.stmts.push_back(
            parseStmt()
        );
    }

    consume(TokenType::RBRACE, "expected '}'");

    return stmt;
}

std::unique_ptr<Stmt> ParseState::parseSwitchStmt() {

    const Token& tok = peek();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::SwitchCase;

    consume(TokenType::LPAREN,
        "expected '(' after switch");

    stmt->switch_stmt.expr = parseExpr();

    consume(TokenType::RPAREN,
        "expected ')' after switch condition");

    consume(TokenType::LBRACE,
        "expected '{' after switch");

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN))
    {
        Stmt::SwitchStmt::Case scase;

        // -------------------
        // case
        // -------------------
        if (match(TokenType::KW_CASE)) {

            scase.is_default = false;

            scase.values = parseLiteral(tok);

            consume(TokenType::COLON,
                "expected ':' after case");

            scase.body = parseStmt();

            stmt->switch_stmt.cases.push_back(
                std::move(scase));

            continue;
        }

        // -------------------
        // default
        // -------------------
        if (match(TokenType::KW_DEFAULT)) {

            scase.is_default = true;

            consume(TokenType::COLON,
                "expected ':' after default");

            scase.body = parseStmt();

            stmt->switch_stmt.cases.push_back(
                std::move(scase));

            continue;
        }

        error("expected case/default");
    }

    consume(TokenType::RBRACE,
        "expected '}' after switch");

    return stmt;
}

std::unique_ptr<UseDecl> ParseState::parseUseDecl() {
    std::unique_std<UseDecl> decl = std::make_unique<UseDecl>();
    decl->target =
        parseQualifiedName(tokens, pos);
    consume(TokenType::KW_AS,
        "expected 'as' after qualified name");
    if (peek().type != TokenType::IDENT)
        error("expected alias identifier");
    decl->alias = advance().value;
    consume(TokenType::SEMICOLON,
        "expected ';' after use declaration");
    return decl;
}
