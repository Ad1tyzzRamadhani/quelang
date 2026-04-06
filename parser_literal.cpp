std::unique_ptr<Literal> parseLiteral( const Token& tok) {
    auto lit = std::make_unique<Literal>();
    lit->line_start = tok.line;
    lit->column_start = tok.column;

    switch(tok.type) {
        case TokenType::NUMBER:
            lit->kind = Literal::Kind::Number;
            lit->value = tok.value;
            break;
        case TokenType::FLOAT:
            lit->kind = Literal::Kind::Float;
            lit->value = tok.value;
            break;
        case TokenType::HEXNUMBER:
            lit->kind = Literal::Kind::Hex;
            lit->value = tok.value;
            break;
        case TokenType::BINARYNUMBER:
            lit->kind = Literal::Kind::Binary;
            lit->value = tok.value;
            break;
        case TokenType::STRING:
            lit->kind = Literal::Kind::String;
            lit->value = tok.value;
            break;
        case TokenType::RAW_STRING:
            lit->kind = Literal::Kind::RawString;
            lit->value = tok.value;
            break;
        case TokenType::CHAR_LITERAL:
            lit->kind = Literal::Kind::Char;
            lit->value = tok.value;
            break;
        case TokenType::KW_TRUE:
            lit->kind = Literal::Kind::True;
            lit->value = "true";
            break;
        case TokenType::KW_FALSE:
            lit->kind = Literal::Kind::False;
            lit->value = "false";
            break;
        case TokenType::KW_NULL:
            lit->kind = Literal::Kind::Null;
            lit->value = "null";
            break;
        default:
            throw std::runtime_error("Unexpected token for literal");
    }

    return lit;
}

std::unique_ptr<QualifiedName> parseQualifiedName(std::vector<Token>& tokens, size_t& pos) {
    auto qn = std::make_unique<QualifiedName>();
    qn->line_start = tokens[pos].line;
    qn->column_start = tokens[pos].column;

    while(pos < tokens.size()) {
        if(tokens[pos].type != TokenType::IDENT)
            break;

        qn->parts.push_back(tokens[pos].value);
        pos++;

        if(pos < tokens.size() && tokens[pos].type == TokenType::SCOPE) {
            pos++;
        } else {
            break;
        }
    }

    if(qn->parts.empty())
        throw std::runtime_error("Expected identifier for qualified name");

    return qn;
}
