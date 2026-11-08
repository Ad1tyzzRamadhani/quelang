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
        const Token& ifTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::If;
        stmt->file = ifTok.file;
        stmt->line_start = ifTok.line;
        stmt->column_start = ifTok.column;


        consume(TokenType::LPAREN, "expected '(' after if keyword");

        stmt->if_stmt.cond = parseExpr();

        consume(TokenType::RPAREN, "expected ')' after if condition");

        stmt->if_stmt.then_block = parseStmt();

        // elif chain
        while (match(TokenType::KW_ELIF)) {
            consume(TokenType::LPAREN,"expected '(' after 'elif' keyword");
            auto cond = parseExpr();
            consume(TokenType::RPAREN,"expected ')' after 'elif' condition expression");
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
        const Token& whileTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::While;
        stmt->file = whileTok.file;
        stmt->line_start = whileTok.line;
        stmt->column_start = whileTok.column;


        consume(TokenType::LPAREN, "expected '(' after 'while' keyword");
        stmt->while_stmt.cond = parseExpr();
        consume(TokenType::RPAREN, "expected ')' after 'while' condition expression");

        stmt->while_stmt.body = parseStmt();

        return stmt;
    }

    if (match(TokenType::KW_SWITCH))
        return parseSwitchStmt();
    // -------------------------
    // DO WHILE
    // -------------------------
    if (match(TokenType::KW_DO)) {
        const Token& doTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::DoStmt;
        stmt->file = doTok.file;
        stmt->line_start = doTok.line;
        stmt->column_start = doTok.column;


        stmt->do_stmt.body = parseStmt();

        if (match(TokenType::KW_WHILE)) {
            consume(TokenType::LPAREN, "expected '(' after 'while' in do-while statement");
            stmt->do_stmt.cond = parseExpr();
            consume(TokenType::RPAREN,"expected ')' after 'while' condition in do-while statement");
            consume(TokenType::SEMICOLON,"expected ';' after do-while statement");

            return stmt;
        }
        if (match(TokenType::KW_CATCH)) {
            consume(TokenType::LPAREN,"expected '(' after 'catch' in do-catch statement");
            stmt->do_stmt.error_var = parseVarDecl();
            consume(TokenType::RPAREN,"expected ')' after error variable declaration in do-catch statement");
            stmt->do_stmt.catch_body = parseStmt();
            stmt->do_stmt.iscatch = true;

            return stmt;
        }
        error("expected 'while' or 'catch' keyword after 'do' statement body");
    }

    // -------------------------
    // RETURN
    // -------------------------
    if (match(TokenType::KW_RETURN)) {
        const Token& retTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Return;
        stmt->file = retTok.file;
        stmt->line_start = retTok.line;
        stmt->column_start = retTok.column;


        if (!check(TokenType::SEMICOLON))
            stmt->return_values = parseExpr();

        consume(TokenType::SEMICOLON,"expected ';' after return statement");

        return stmt;
    }

    // -------------------------
    // BREAK
    // -------------------------
    if (match(TokenType::KW_BREAK)) {
        const Token& breakTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Break;
        stmt->file = breakTok.file;
        stmt->line_start = breakTok.line;
        stmt->column_start = breakTok.column;

        consume(TokenType::SEMICOLON,"expected ';' after break statement");
        return stmt;
    }

    // -------------------------
    // CONTINUE
    // -------------------------
    if (match(TokenType::KW_CONTINUE)) {
        const Token& contTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Continue;
        stmt->file = contTok.file;
        stmt->line_start = contTok.line;
        stmt->column_start = contTok.column;

        consume(TokenType::SEMICOLON,"expected ';' after continue statement");
        return stmt;
    }

    if (match(TokenType::KW_UNSAFE)) {
        const Token& unsafeTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Unsafe;
        stmt->file = unsafeTok.file;
        stmt->line_start = unsafeTok.line;
        stmt->column_start = unsafeTok.column;

        stmt->unsafe_stmt.stmt = parseStmt();

        return stmt;
    }

    if (match(TokenType::KW_THROW)) {
        const Token& throwTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::ThrowStmt;
        stmt->file = throwTok.file;
        stmt->line_start = throwTok.line;
        stmt->column_start = throwTok.column;

        stmt->throw_stmt.expr = parseExpr();
        consume(TokenType::SEMICOLON,"expected ';' after throw expression");

        return stmt;
    }

    // -------------------------
    // DROP / WIPE
    // -------------------------
    if (match(TokenType::KW_DROP)) {
        const Token& dropTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Drop;
        stmt->file = dropTok.file;
        stmt->line_start = dropTok.line;
        stmt->column_start = dropTok.column;

        stmt->drop_stmt.expr = parseExpr();
        consume(TokenType::SEMICOLON,"expected ';' after drop statement");

        return stmt;
    }

    if (match(TokenType::KW_WIPE)) {
        const Token& wipeTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Wipe;
        stmt->file = wipeTok.file;
        stmt->line_start = wipeTok.line;
        stmt->column_start = wipeTok.column;

        stmt->wipe_stmt.expr = parseExpr();
        consume(TokenType::SEMICOLON,"expected ';' after wipe statement");

        return stmt;
    }

    // -------------------------
    // LABEL
    // -------------------------
    if (tok.type == TokenType::IDENT && peek(1).type == TokenType::COLON) {
        const Token& labelTok = tok;
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Label;
        stmt->file = labelTok.file;
        stmt->line_start = labelTok.line;
        stmt->column_start = labelTok.column;

        stmt->label = advance().value;
        consume(TokenType::COLON,"expected ':' after label identifier");

        return stmt;
    }

    // -------------------------
    // JUMPTO
    // -------------------------
    if (match(TokenType::KW_JUMPTO)) {
        const Token& jumpTok = tokens[pos - 1];
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Jump;
        stmt->file = jumpTok.file;
        stmt->line_start = jumpTok.line;
        stmt->column_start = jumpTok.column;

        if (!check(TokenType::IDENT)) {
            error("expected target label identifier after 'jumpto'");
        }
        stmt->jump_target = advance().value;
        consume(TokenType::SEMICOLON,"expected ';' after jumpto target statement");

        return stmt;
    }

    // -------------------------
    // VARIABLE DECL / EXPRESSIONS
    // -------------------------
    if (isVarDeclAhead()) {
        const Token& varTok = peek();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::VarDecl;
        stmt->file = varTok.file;
        stmt->line_start = varTok.line;
        stmt->column_start = varTok.column;

        stmt->var_decl = parseVarDecl();
        consume(TokenType::SEMICOLON,"expected ';' after variable declaration");

        return stmt;
    }

    // -------------------------
    // FALLBACK: ExprStmt
    // -------------------------
    {
        const Token& exprTok = peek();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::ExprStmt;
        stmt->file = exprTok.file;
        stmt->line_start = exprTok.line;
        stmt->column_start = exprTok.column;

        stmt->expr_stmt = parseExpr();
        consume(TokenType::SEMICOLON,"expected ';' after expression statement");

        return stmt;
    }
}

VarDecl ParseState::parseVarDecl(Visibility visibility) {
    VarDecl decl;
    const Token& tok = peek();
    decl.file = tok.file;
    decl.line_start = tok.line;
    decl.column_start = tok.column;
    decl.visibility = visibility;

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

        VarDecl::Item item;

        if (peek().type != TokenType::IDENT)
            error("expected variable identifier name in declaration list");

        item.name = advance().value;

        if (match(TokenType::COLON)) {
            if (peek().type == TokenType::NUMBER)
            decl.type->bit_width = parseLiteral(advance());
            else error("expected numeric literal for bit width specification after colon");
        }
        if (match(TokenType::LBRACE)) {
            if (peek().type != TokenType::RBRACE) {
                item.args = parseArgList();
            }
            consume(TokenType::RBRACE,"expected '}' to close variable initialization arguments");
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
    const Token& braceTok = peek();
    consume(TokenType::LBRACE, "expected '{' to open code block");

    auto stmt = std::make_unique<Stmt>();
    stmt->file = braceTok.file;
    stmt->line_start = braceTok.line;
    stmt->column_start = braceTok.column;
    stmt->kind = Stmt::Kind::Block;

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN))
    {
        stmt->block.stmts.push_back(
            parseStmt()
        );
    }

    consume(TokenType::RBRACE, "expected '}' to close code block");

    return stmt;
}

std::unique_ptr<Stmt> ParseState::parseSwitchStmt() {
    const Token& tok = peek();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::SwitchCase;
    stmt->file = tok.file;
    stmt->line_start = tok.line;
    stmt->column_start = tok.column;

    consume(TokenType::LPAREN,
        "expected '(' after 'switch' keyword");

    stmt->switch_stmt.expr = parseExpr();

    consume(TokenType::RPAREN,
        "expected ')' after 'switch' condition expression");

    consume(TokenType::LBRACE,
        "expected '{' to open switch body block");

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN))
    {
        Stmt::SwitchStmt::Case scase;

        // -------------------
        // case
        // -------------------
        if (match(TokenType::KW_CASE)) {

            scase.is_default = false;

            scase.values.push_back(parseExpr());

            consume(TokenType::COLON,
                "expected ':' after switch case value expression");

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
                "expected ':' after 'default' keyword");

            scase.body = parseStmt();

            stmt->switch_stmt.cases.push_back(
                std::move(scase));

            continue;
        }

        error("expected 'case' or 'default' keyword inside switch body");
    }

    consume(TokenType::RBRACE,
        "expected '}' to close switch body block");

    return stmt;
}
