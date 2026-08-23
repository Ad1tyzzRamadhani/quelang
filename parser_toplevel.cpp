Visibility ParseState::parseVisibility() {
    if (match(TokenType::KW_PUBLIC))
        return Visibility::Public;

    return Visibility::Private;
}

std::unique_ptr<FileProgram> ParseState::parseProgram() {

    auto program = std::make_unique<FileProgram>();

    if (!tokens.empty()) {
        program->file = tokens.front().file;
        program->line_start = tokens.front().line;
        program->column_start = tokens.front().column;
    }

    while (!check(TokenType::EOF_TOKEN)) {

        auto decl = parseTopLevelDecl();

        if (!decl)
            error("failed to parse top-level declaration");

        program->decls.push_back(std::move(decl));
    }

    if (!tokens.empty()) {
        program->line_end = tokens.back().line;
        program->column_end = tokens.back().column;
    }

    return program;
}

std::unique_ptr<Node> ParseState::parseTopLevelDecl() {

    Visibility visibility = parseVisibility();

    switch (peek().type) {

        case TokenType::KW_NAMESPACE:
            return parseNamespaceDecl();

        case TokenType::KW_USE:
            return parseUseDecl();

        case TokenType::KW_STRUCT:
            return parseStructDef(visibility);

        case TokenType::KW_ENUM:
            return parseEnumDef(visibility);

        case TokenType::KW_UNION:
            return parseUnionDef(visibility);

        /*
         * Kalau nanti InterfaceDef sudah ada di AST:
         *
         * case TokenType::KW_INTERFACE:
         *     return parseInterfaceDef(visibility);
         */

        case TokenType::KW_STATIC:
        case TokenType::KW_EXTERN:
        case TokenType::KW_NORETURN:
            break;

        default:
            break;
    }

    /*
     * Setelah visibility/storage/attribute,
     * sisanya bisa merupakan:
     *
     *   Type IDENT (...)
     *   Type IDENT ...
     *
     * yaitu function atau global variable.
     */

    if (isForwardDeclAhead()) {
        return parseForwardDecl(visibility);
    }

    /*
     * Kalau setelah type + identifier terdapat '(',
     * berarti function definition.
     */
    if (isFunctionDeclarationAhead()) {
        return parseFunction(visibility);
    }

    /*
     * Selain itu dianggap global variable.
     */
    auto var = parseVarDecl(visibility);

    consume(
        TokenType::SEMICOLON,
        "expected ';' after global variable declaration"
    );

    return std::make_unique<VarDecl>(std::move(var));
}

bool ParseState::isFunctionDeclarationAhead() {

    size_t save = pos;

    try {

        // Function attributes
        while (
            check(TokenType::KW_STATIC) ||
            check(TokenType::KW_EXTERN) ||
            check(TokenType::KW_NORETURN)
        ) {
            advance();
        }

        // qualifiers
        while (
            check(TokenType::KW_CONST) ||
            check(TokenType::KW_VOLATILE) ||
            check(TokenType::KW_ATOMIC)
        ) {
            advance();
        }

        /*
         * Function type sebagai return type
         */
        if (check(TokenType::LPAREN)) {
            parseFuncPtrType({});
        } else {
            parseType();
        }

        if (!check(TokenType::IDENT)) {
            pos = save;
            return false;
        }

        advance();

        bool result = check(TokenType::LPAREN);

        pos = save;
        return result;

    } catch (...) {

        pos = save;
        return false;
    }
}

bool ParseState::isForwardDeclAhead() {

    size_t save = pos;

    try {

        /*
         * -------------------------------------------------
         * Function / declaration attributes
         * -------------------------------------------------
         */
        while (
            check(TokenType::KW_STATIC) ||
            check(TokenType::KW_EXTERN) ||
            check(TokenType::KW_NORETURN)
        ) {
            advance();
        }

        /*
         * -------------------------------------------------
         * struct Foo;
         * -------------------------------------------------
         */
        if (check(TokenType::KW_STRUCT)) {

            advance();

            /*
             * Nama struct
             */
            if (!check(TokenType::IDENT)) {
                pos = save;
                return false;
            }

            parseQualifiedName();

            /*
             * Forward struct harus langsung ';'
             *
             * struct Foo;
             */
            bool result = check(TokenType::SEMICOLON);

            pos = save;
            return result;
        }

        /*
         * -------------------------------------------------
         * Function forward declaration
         * -------------------------------------------------
         *
         * int foo(...);
         *
         * static int foo(...);
         * extern int foo(...);
         * noreturn int foo(...);
         *
         * atau return type function pointer:
         *
         * (int()) foo(...);
         */
        while (
            check(TokenType::KW_CONST) ||
            check(TokenType::KW_VOLATILE) ||
            check(TokenType::KW_ATOMIC)
        ) {
            advance();
        }

        /*
         * Return type
         */
        if (check(TokenType::LPAREN)) {
            parseFuncPtrType({});
        } else {
            parseType();
        }

        /*
         * Function name
         */
        if (!check(TokenType::IDENT)) {
            pos = save;
            return false;
        }

        advance();

        /*
         * Harus function:
         *
         * int foo(...)
         *        ^
         */
        if (!check(TokenType::LPAREN)) {
            pos = save;
            return false;
        }

        /*
         * Lewati parameter list hanya untuk lookahead.
         */
        int depth = 0;

        do {
            if (check(TokenType::LPAREN)) {
                depth++;
            }
            else if (check(TokenType::RPAREN)) {
                depth--;
            }

            advance();

        } while (
            depth > 0 &&
            !check(TokenType::EOF_TOKEN)
        );

        /*
         * Setelah ')' boleh:
         *
         * const ;
         * co ;
         * co const ;
         * ;
         */

        if (match(TokenType::KW_CO)) {
            match(TokenType::KW_CONST);
        }
        else {
            match(TokenType::KW_CONST);
        }

        bool result = check(TokenType::SEMICOLON);

        pos = save;
        return result;

    } catch (...) {

        pos = save;
        return false;
    }
}

std::unique_ptr<Function> ParseState::parseFunction(Visibility visibility) {

    auto fn = std::make_unique<Function>();

    fn->visibility = visibility;

    fn->line_start = peek().line;
    fn->column_start = peek().column;
    fn->file = peek().file;

    /*
     * FunctionAttr*
     */
    while (true) {

        if (match(TokenType::KW_STATIC)) {
            fn->is_static = true;
            continue;
        }

        if (match(TokenType::KW_EXTERN)) {
            fn->is_extern = true;
            continue;
        }

        if (match(TokenType::KW_NORETURN)) {
            fn->is_noreturn = true;
            continue;
        }

        break;
    }

    /*
     * Return type
     */
    fn->return_types = parseType();

    /*
     * Function name
     */
    fn->name = parseQualifiedName();

    /*
     * (
     */
    consume(
        TokenType::LPAREN,
        "expected '(' after function name"
    );

    /*
     * ParamListOpt
     */
    if (!check(TokenType::RPAREN)) {

        while (true) {

            Function::Param param;

            param.type = parseType();

            if (!check(TokenType::IDENT))
                error("expected parameter name");

            param.name = advance().value;

            if (match(TokenType::EQ)) {
                param.init = parseExpr();
            }

            fn->params.push_back(std::move(param));

            if (!match(TokenType::COMMA))
                break;
        }
    }

    consume(
        TokenType::RPAREN,
        "expected ')' after function parameters"
    );

    /*
     * CoroutineOpt
     */
    if (match(TokenType::KW_CO)) {
        fn->is_coroutine = true;
    }

    /*
     * ConstOpt
     */
    if (match(TokenType::KW_CONST)) {
        fn->is_const = true;
    }

    /*
     * Body
     */
    fn->body = parseBlock();

    return fn;
}

std::unique_ptr<ForwardDecl> ParseState::parseForwardDecl(Visibility visibility) {

    auto decl = std::make_unique<ForwardDecl>();

    decl->visibility = visibility;

    decl->line_start = peek().line;
    decl->column_start = peek().column;
    decl->file = peek().file;

    /*
     * struct Foo;
     */
    if (match(TokenType::KW_STRUCT)) {

        decl->kind = ForwardDecl::Kind::Struct;

        decl->name = parseQualifiedName();

        consume(
            TokenType::SEMICOLON,
            "expected ';' after struct forward declaration"
        );

        return decl;
    }

    /*
     * Function forward declaration
     */
    decl->kind = ForwardDecl::Kind::Function;

    while (true) {

        if (match(TokenType::KW_STATIC)) {
            decl->is_static = true;
            continue;
        }

        if (match(TokenType::KW_EXTERN)) {
            // ForwardDecl AST belum punya is_extern.
            continue;
        }

        if (match(TokenType::KW_NORETURN)) {
            continue;
        }

        break;
    }

    decl->return_types = parseType();

    decl->name = parseQualifiedName();

    consume(
        TokenType::LPAREN,
        "expected '(' in function declaration"
    );

    if (!check(TokenType::RPAREN)) {

        while (true) {

            auto paramType = parseType();

            /*
             * ForwardDecl hanya menyimpan Type parameter,
             * sesuai AST sekarang.
             */
            decl->params.push_back(std::move(paramType));

            /*
             * Grammar Forward Function menggunakan
             * FunctionDecl -> ParamList, sehingga parameter
             * sebenarnya punya nama.
             *
             * Kalau nama parameter tidak disimpan,
             * consume saja.
             */
            if (check(TokenType::IDENT))
                advance();

            if (!match(TokenType::COMMA))
                break;
        }
    }

    consume(
        TokenType::RPAREN,
        "expected ')' in function declaration"
    );

    /*
     * coroutine
     */
    if (match(TokenType::KW_CO)) {
        // AST ForwardDecl belum punya is_coroutine.
    }

    /*
     * const
     */
    match(TokenType::KW_CONST);

    consume(
        TokenType::SEMICOLON,
        "expected ';' after function declaration"
    );

    return decl;
}

std::unique_ptr<NamespaceDecl> ParseState::parseNamespaceDecl() {

    auto ns = std::make_unique<NamespaceDecl>();

    ns->line_start = peek().line;
    ns->column_start = peek().column;
    ns->file = peek().file;

    consume(
        TokenType::KW_NAMESPACE,
        "expected 'namespace'"
    );

    ns->name = parseQualifiedName();

    consume(
        TokenType::LBRACE,
        "expected '{' after namespace name"
    );

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN)) {

        ns->items.push_back(
            parseTopLevelDecl()
        );
    }

    consume(
        TokenType::RBRACE,
        "expected '}' after namespace"
    );

    return ns;
}

std::unique_ptr<StructDef> ParseState::parseStructDef(Visibility visibility) {

    auto st = std::make_unique<StructDef>();

    st->visibility = visibility;

    st->line_start = peek().line;
    st->column_start = peek().column;
    st->file = peek().file;

    consume(
        TokenType::KW_STRUCT,
        "expected 'struct'"
    );

    st->name = parseQualifiedName();

    /*
     * ImplementsOpt
     *
     * struct Foo(Base, Interface)
     */
    if (match(TokenType::LPAREN)) {

        while (!check(TokenType::RPAREN)) {

            auto base = parseQualifiedName();

            /*
             * AST StructDef sekarang belum punya
             * field untuk implements/base.
             *
             * Sebaiknya tambahkan:
             *
             * std::vector<std::unique_ptr<QualifiedName>> implements;
             */

            if (!match(TokenType::COMMA))
                break;
        }

        consume(
            TokenType::RPAREN,
            "expected ')' after struct inheritance list"
        );
    }

    consume(
        TokenType::LBRACE,
        "expected '{' after struct name"
    );

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN)) {

        /*
         * constructor
         */
        if (check(TokenType::KW_CONSTRUCT)) {

            auto ctor = std::make_unique<Constructor>();

            advance();

            consume(TokenType::LPAREN);

            if (!check(TokenType::RPAREN)) {

                while (true) {

                    Constructor::Param param;

                    param.type = parseType();

                    if (!check(TokenType::IDENT))
                        error("expected constructor parameter name");

                    param.name = advance().value;

                    if (match(TokenType::EQ))
                        param.init = parseExpr();

                    ctor->params.push_back(
                        std::move(param)
                    );

                    if (!match(TokenType::COMMA))
                        break;
                }
            }

            consume(TokenType::RPAREN);

            ctor->body = parseBlock();

            st->ctor = std::move(ctor);

            continue;
        }

        /*
         * destructor
         */
        if (check(TokenType::KW_DROP)) {

            auto dtor = std::make_unique<Destructor>();

            advance();

            consume(TokenType::LPAREN);
            consume(
                TokenType::RPAREN,
                "expected ')' after drop"
            );

            dtor->body = parseBlock();

            st->dtor = std::move(dtor);

            continue;
        }

        /*
         * default = field;
         */
        if (match(TokenType::KW_DEFAULT)) {

            consume(
                TokenType::EQ,
                "expected '=' after default"
            );

            if (!check(TokenType::IDENT))
                error("expected default field name");

            st->default_field = advance().value;

            consume(
                TokenType::SEMICOLON,
                "expected ';' after default field"
            );

            continue;
        }
        
        Visibility method_vis = parseVisibility();
        /*
         * nested struct
         */
        if (check(TokenType::KW_STRUCT)) {
            st->members.push_back(
                parseStructDef(method_vis)
            );
            continue;
        }

        /*
         * nested enum
         */
        if (check(TokenType::KW_ENUM)) {
            st->members.push_back(
                parseEnumDef(method_vis)
            );
            continue;
        }

        /*
         * nested union
         */
        if (check(TokenType::KW_UNION)) {
            st->members.push_back(
                parseUnionDef(method_vis)
            );
            continue;
        }

        /*
         * function / field
         *
         * Ini butuh parser member-dispatcher yang lebih
         * lengkap daripada top-level.
         */
        if (isFunctionDeclarationAhead()) {

            st->members.push_back(
                parseFunction(method_vis)
            );

            continue;
        }

        /*
         * FieldDecl
         */
        auto field = parseVarDecl();

        consume(
            TokenType::SEMICOLON,
            "expected ';' after struct field"
        );

        st->members.push_back(
            std::make_unique<VarDecl>(std::move(field))
        );
    }

    consume(
        TokenType::RBRACE,
        "expected '}' after struct definition"
    );

    return st;
}

std::unique_ptr<UnionDef> ParseState::parseUnionDef(Visibility visibility) {

    auto un = std::make_unique<UnionDef>();

    un->visibility = visibility;

    consume(
        TokenType::KW_UNION,
        "expected 'union'"
    );

    un->name = parseQualifiedName();

    consume(
        TokenType::LBRACE,
        "expected '{' after union name"
    );

    while (!check(TokenType::RBRACE) &&
           !check(TokenType::EOF_TOKEN)) {

        if (match(TokenType::KW_DEFAULT)) {

            consume(
                TokenType::EQ,
                "expected '=' after default"
            );

            if (!check(TokenType::IDENT))
                error("expected union default field");

            un->default_field = advance().value;

            consume(
                TokenType::SEMICOLON,
                "expected ';' after default field"
            );

            continue;
        }

        UnionDef::Field field;

        field.type = parseType();

        if (!check(TokenType::IDENT))
            error("expected union field name");

        field.name = advance().value;

        consume(
            TokenType::SEMICOLON,
            "expected ';' after union field"
        );

        un->fields.push_back(
            std::move(field)
        );
    }

    consume(
        TokenType::RBRACE,
        "expected '}' after union"
    );

    return un;
}

std::unique_ptr<EnumDef> ParseState::parseEnumDef(Visibility visibility) {

    auto en = std::make_unique<EnumDef>();

    en->visibility = visibility;

    consume(
        TokenType::KW_ENUM,
        "expected 'enum'"
    );

    en->name = parseQualifiedName();

    /*
     * EnumBaseOpt
     */
    if (match(TokenType::COLON)) {
        en->underlying_type = parseType();
    }

    consume(
        TokenType::LBRACE,
        "expected '{' after enum name"
    );

    if (!check(TokenType::RBRACE)) {

        while (true) {

            if (!check(TokenType::IDENT))
                error("expected enum item");

            EnumDef::Item item;

            item.name = advance().value;

            if (match(TokenType::EQ)) {
                item.value = parseExpr();
            }

            en->items.push_back(
                std::move(item)
            );

            if (!match(TokenType::COMMA))
                break;

            /*
             * trailing comma
             */
            if (check(TokenType::RBRACE))
                break;
        }
    }

    consume(
        TokenType::RBRACE,
        "expected '}' after enum"
    );

    return en;
}

