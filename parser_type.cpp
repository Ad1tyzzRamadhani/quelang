std::unique_ptr<Type> parseType() {
    auto type = std::make_unique<Type>();
    while (true) {
        if (match(TokenType::KW_CONST)) {
            type->qualifiers.push_back(
                TypeQualifier::Const
            );
        }
        else if (match(TokenType::KW_VOLATILE)) {
            type->qualifiers.push_back(
                TypeQualifier::Volatile
            );
        }
        else {
            break;
        }
    }

    type->base = parseQualifiedName(tokens, pos);
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

        // function pointer
        if (peek().type == TokenType::LPAREN) {
            TypeModifier mod;
            consume(TokenType::LPAREN);
            do {
                mod.func_return.push_back(
                parseType()
                );
            }
            while(match(TokenType::COMMA));

            consume(TokenType::RPAREN);

            size_t backup = pos;

            try {

                advance(); // (

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

                if (
                    check(TokenType::STAR) ||
                    check(TokenType::AMP)
                ) {

                    mod.kind =
                        TypeModifier::Kind::FuncPtr;

                    mod.func_params =
                        std::move(params);

                    if (match(TokenType::STAR)) {
                        // func ptr
                    }
                    else {
                        consume(
                            TokenType::AMP,
                            "expected * or & after function type"
                        );
                    }

                    type->modifiers.push_back(
                        std::move(mod)
                    );

                    continue;
                }

                pos = backup;
                break;
            }
            catch (...) {
                pos = backup;
                break;
            }
        }

        break;
    }

    return type;
}
