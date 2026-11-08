std::unique_ptr<Type> ParseState::parseType() {
    auto type = std::make_unique<Type>();
    if (!tokens.empty()) {
        type->file = peek().file;
        type->line_start = peek().line;
        type->column_start = peek().column;
    }
    bool has_unsized_array = false;
    while (match(TokenType::LBRACKET)) {
        if (check(TokenType::NUMBER) || check(TokenType::HEXNUMBER)) {
            type->array_dims.push_back(parseLiteral(advance()));
            type->is_array = true;
        } else {
            // Unsized array [] (slice) dianggap sebagai pointer ke tipe dasar
            has_unsized_array = true;
            type->is_array = true;
        }
        consume(TokenType::RBRACKET, "expected ']' to close array dimension or slice definition");
    }
    while (true) {
        if (match(TokenType::KW_CONST)) {
            type->qualifiers.push_back(
                TypeQualifier::Const
            );
            continue;
        }
        if (match(TokenType::KW_VOLATILE)) {
            type->qualifiers.push_back(
                TypeQualifier::Volatile
            );
            continue;
        }
        if (match(TokenType::KW_ATOMIC)) {
            type->qualifiers.push_back(
                TypeQualifier::Atomic
            );
            continue;
        }
        break;
    }
    
    if (peek().type == TokenType::LPAREN) return parseFuncPtrType(type->qualifiers);

    type->base = parseQualifiedName();
    
    while (true) {

        // pointer
        if (match(TokenType::STAR)) {
            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Pointer;

            type->modifiers.push_back(
                std::move(mod)
            );

            continue;
        }

        // reference
        if (match(TokenType::AMP)) {
            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Reference;

            type->modifiers.push_back(
                std::move(mod)
            );

            continue;
        }

        if (match(TokenType::KW_RESTRICT)) {
            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Restrict;

            type->modifiers.push_back(
                std::move(mod)
            );

            continue;
        }
        
        break;
    }
    if (std::find_if(type->modifiers.begin(), type->modifiers.end(), [](const TypeModifier& t) {return t.kind == TypeModifier::Kind::Pointer;}) != type->modifiers.end()) {
        if(type->is_array) {
            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Pointer;
            type->modifiers.push_back(std::move(mod));
        }
    }
    return type;
}

std::unique_ptr<Type> ParseState::parseFuncPtrType(std::vector<TypeQualifier> tq) {
        // function pointer
    auto type = std::make_unique<Type>();
    if (!tokens.empty()) {
        type->file = peek().file;
        type->line_start = peek().line;
        type->column_start = peek().column;
    }
    type->qualifiers = tq;
    type->base = MakeEmptyQualifiedName();
        if (peek().type == TokenType::LPAREN) {
            TypeModifier mod;
            consume(TokenType::LPAREN, "expected '(' at the beginning of function pointer return type");
            if (!check(TokenType::RPAREN)) {
                mod.func_return = parseType();
            }

            consume(TokenType::RPAREN,"expected ')' after function pointer return type");

            size_t backup = pos;

            try {

                consume(TokenType::LPAREN,"expected '(' for function pointer parameters list");

                std::vector<std::unique_ptr<Type>> params;

                if (!check(TokenType::RPAREN)) {

                    do {
                        params.push_back(
                            parseType()
                        );
                    }
                    while (match(TokenType::COMMA));
                }

                consume(
                    TokenType::RPAREN,
                    "expected ')' to close function pointer parameter list"
                );
                if (check(TokenType::KW_THROWS)) {
                    consume(TokenType::KW_THROWS, " THROWS FUNCPTR");
                    mod.throws_type = parseType();
                }
                if (
                    check(TokenType::STAR)
                ) {

                    mod.kind =
                        TypeModifier::Kind::FuncPtr;

                    mod.func_params =
                        std::move(params);

                    consume(TokenType::STAR, "expected '*' to complete function pointer signature");

                    type->modifiers.push_back(
                        std::move(mod)
                    );
                } else {
                    error("expected '*' to finalize function pointer declaration");
                }
            }
            catch (...) {
                pos = backup;
                error("unknown error occurred while parsing function pointer type");
            }
        }
    return type;
}
