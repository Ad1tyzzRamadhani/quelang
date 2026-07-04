struct ParseState {
    const std::vector<Token>& tokens;
    size_t pos = 0;

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
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseStmt();

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
    return parsePostfix();
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
