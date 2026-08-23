std::unique_ptr<Type> ParseState::parseType() {
    auto type = std::make_unique<Type>();
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
    return type;
}

std::unique_ptr<Type> ParseState::parseFuncPtrType(std::vector<TypeQualifier> tq) {
        // function pointer
    auto type = std::make_unique<Type>();
    type->qualifiers = tq;
        if (peek().type == TokenType::LPAREN) {
            TypeModifier mod;
            consume(TokenType::LPAREN);
            if (!check(TokenType::RPAREN)) {
                mod.func_return = parseType();
            }

            consume(TokenType::RPAREN);

            size_t backup = pos;

            try {

                consume(TokenType::LPAREN);

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
                    "expected ')' in function pointer"
                );
                if (check(TokenType::KW_CO)) {
                    consume(TokenType::KW_CO);
                    mod.is_coroutine = true;
                }
                if (
                    check(TokenType::STAR)
                ) {

                    mod.kind =
                        TypeModifier::Kind::FuncPtr;

                    mod.func_params =
                        std::move(params);

                    consume(TokenType::STAR);

                    type->modifiers.push_back(
                        std::move(mod)
                    );
                }
            }
            catch (...) {
                pos = backup;
            }
        }
    return type;
}
