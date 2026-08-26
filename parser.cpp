struct ParseState {
    const std::vector<Token>& tokens;
    size_t pos = 0;

    std::unique_ptr<QualifiedName> parseQualifiedName();
    std::unique_ptr<Type> parseFuncPtrType(std::vector<TypeQualifier> tq);
    std::unique_ptr<Type> parseType();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parseCast();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parseMul();
    std::unique_ptr<Expr> parseAdd();
    std::unique_ptr<Expr> parseBitwiseAnd();
    std::unique_ptr<Expr> parseBitwiseXor();
    std::unique_ptr<Expr> parseBitwiseOr();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseLogic();
    std::unique_ptr<Expr> parseCoalesce();
    std::unique_ptr<Expr> parseTernary();
    std::unique_ptr<Expr> parseAssign();
    std::vector<std::unique_ptr<Expr>> parseArgList();
    std::vector<std::unique_ptr<Expr>> parseExprList();
    Visibility parseVisibility();
    std::unique_ptr<FileProgram> parseProgram();
    std::unique_ptr<Node> parseTopLevelDecl();
    bool isFunctionDeclarationAhead();
    std::unique_ptr<Function> parseFunction(Visibility visibility);
    std::unique_ptr<ForwardDecl> parseForwardDecl(Visibility visibility);
    std::unique_ptr<NamespaceDecl> parseNamespaceDecl();
    std::unique_ptr<EnumDef> parseEnumDef(Visibility visibility);
    std::unique_ptr<UnionDef> parseUnionDef(Visibility visibility);
    std::unique_ptr<StructDef> parseStructDef(Visibility visibility);
    bool isPrimitiveType(TokenType t);
    bool isForwardDeclAhead();
    bool isTypeStart(const Token& tok);
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Stmt> parseStmt();
    std::unique_ptr<Stmt> parseBlock();
    VarDecl parseVarDecl(Visibility visibility = Visibility::Private);
    std::unique_ptr<Stmt> parseSwitchStmt();
    std::unique_ptr<UseDecl> parseUseDecl();
    bool isVarDeclAhead();

    const Token& peek(int offset = 0) const {
        if (pos + offset >= tokens.size()) return tokens.back();
        return tokens[pos + offset];
    }

    const Token& advance() { return tokens[pos++]; }

    bool match(TokenType t) {
        if (peek().type == t) { advance(); return true; }
        return false;
    }

    void consume(TokenType t, const std::string& msg = "") {
        if (!match(t)) {
            throw std::runtime_error(msg);
        }
    }

    bool check(TokenType type) {
        return peek().type == type;
    }

    [[noreturn]] void error(const std::string& msg) const {
        const auto& tok = peek();
        throw std::runtime_error(tok.file + ":" +
                                 std::to_string(tok.line) + ":" +
                                 std::to_string(tok.column) + " " + msg);
    }
};

std::vector<std::unique_ptr<Expr>> ParseState::parseArgList() {
    std::vector<std::unique_ptr<Expr>> args;

    if (!check(TokenType::RPAREN)) {
        do {
            args.push_back(parseExpr());
        } while (match(TokenType::COMMA));
    }

    return args;
}

std::unique_ptr<Expr> ParseState::parseExpr() {
    return parseAssign();
}

bool ParseState::isTypeStart(const Token& tok) {
    return tok.type == TokenType::IDENT || isPrimitiveType(tok.type);
}

bool ParseState::isVarDeclAhead() {

    size_t save = pos;

    try {

        // StorageOpt
        if (check(TokenType::KW_STATIC) ||
            check(TokenType::KW_EXTERN)) {
            advance();
        }

        // TypeQualifier*
        while (
            check(TokenType::KW_CONST) ||
            check(TokenType::KW_VOLATILE) ||
            check(TokenType::KW_ATOMIC)
        ) {
            advance();
        }

        // BaseType + TypeModifier*
        parseType();

        // VarList harus dimulai IDENT
        if (!check(TokenType::IDENT)) {
            pos = save;
            return false;
        }

        advance();

        // ArrayDimsOpt

        if (match(TokenType::LBRACE)) {
            parseArgList();
            if (!match(TokenType::RBRACE)) {
                pos = save;
                return false;
            }
        }
        while (match(TokenType::LBRACKET)) {

            if (!check(TokenType::NUMBER)) {
                pos = save;
                return false;
            }

            advance();

            if (!match(TokenType::RBRACKET)) {
                pos = save;
                return false;
            }
        }

        // VarInitOpt
        if (match(TokenType::EQ)) {
            parseExpr();
        }

        // Kalau ada comma, masih mungkin VarList
        while (match(TokenType::COMMA)) {

            if (!check(TokenType::IDENT)) {
                pos = save;
                return false;
            }

            advance();

            while (match(TokenType::LBRACKET)) {

                if (!check(TokenType::NUMBER)) {
                    pos = save;
                    return false;
                }

                advance();

                consume(
                    TokenType::RBRACKET,
                    "expected ']'"
                );
            }

            if (match(TokenType::EQ)) {
                parseExpr();
            }
        }

        bool result = check(TokenType::SEMICOLON);

        pos = save;
        return result;

    } catch (...) {

        pos = save;
        return false;
    }
}

bool ParseState::isPrimitiveType(TokenType t) {
    switch (t) {

        case TokenType::I8:
        case TokenType::I16:
        case TokenType::I32:
        case TokenType::I64:

        case TokenType::U8:
        case TokenType::U16:
        case TokenType::U32:
        case TokenType::U64:

        case TokenType::F32:
        case TokenType::F64:

        case TokenType::CHAR8:
        case TokenType::CHAR16:
        case TokenType::CHAR32:

        case TokenType::BOOL:
        case TokenType::VOID:

        case TokenType::USIZE:
        case TokenType::ISIZE:
            return true;

        default:
            return false;
    }
}

std::vector<std::unique_ptr<Expr>>
ParseState::parseExprList() {

    std::vector<std::unique_ptr<Expr>> result;

    result.push_back(parseExpr());

    while (match(TokenType::COMMA)) {
        result.push_back(parseExpr());
    }

    return result;
}
