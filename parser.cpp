struct ParseState {
    const std::vector<Token>& tokens;
    size_t pos = 0;

    std::unique_ptr<Type> parseFuncPtrType(std::vector<TypeQualifier> tq);
    std::unique_ptr<Type> parseType();
    std::unique_ptr<Expr> parsePostfix();
    std::vector<std::unique_ptr<Expr>> parseArgList();
    std::unique_ptr<Expr> parseExpr();

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
