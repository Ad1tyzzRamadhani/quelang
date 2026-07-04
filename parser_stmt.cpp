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

            stmt->if_stmt.elifs.push_back({
                cond,
                parseStmt()
            });
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

    // -------------------------
    // DO WHILE
    // -------------------------
    if (match(TokenType::KW_DO)) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::DoWhile;

        stmt->do_while_stmt.body = parseStmt();

        consume(TokenType::KW_WHILE);
        consume(TokenType::LPAREN);
        stmt->do_while.cond = parseExpr();
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

        stmt->for_stmt.init = parseForInit();

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
            stmt->return_values.expr = parseExpr();

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
