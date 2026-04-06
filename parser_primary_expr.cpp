// Enum Mapping
BinaryOp tokenToBinaryOp(const Token& tok) {
    switch(tok.type) {
        case TokenType::PLUS: return BinaryOp::Add;
        case TokenType::MINUS: return BinaryOp::Sub;
        case TokenType::STAR: return BinaryOp::Mul;
        case TokenType::SLASH: return BinaryOp::Div;
        case TokenType::PERCENT: return BinaryOp::Mod;
        case TokenType::EQEQ: return BinaryOp::Eq;
        case TokenType::NEQ: return BinaryOp::Ne;
        case TokenType::LT: return BinaryOp::Lt;
        case TokenType::LTE: return BinaryOp::Lte;
        case TokenType::GT: return BinaryOp::Gt;
        case TokenType::GTE: return BinaryOp::Gte;
        case TokenType::AMP: return BinaryOp::BitAnd;
        case TokenType::PIPE: return BinaryOp::BitOr;
        case TokenType::CARET: return BinaryOp::BitXor;
        case TokenType::KW_AND: return BinaryOp::And;
        case TokenType::KW_OR: return BinaryOp::Or;
        case TokenType::RANGE: return BinaryOp::In;
        case TokenType::COALESCE: return BinaryOp::Coalesce;
        default: throw std::runtime_error("Unknown binary op");
    }
}

UnaryOp tokenToUnaryOp(const Token& tok) {
    switch(tok.type) {
        case TokenType::MINUS: return UnaryOp::Neg;
        case TokenType::STAR: return UnaryOp::Deref;
        case TokenType::AMP: return UnaryOp::Ref;
        case TokenType::BANG: return UnaryOp::Not;
        case TokenType::TILDE: return UnaryOp::BitNot;
        default: throw std::runtime_error("Unknown unary op");
    }
}

AssignOp tokenToAssignOp(const Token& tok) {
    switch(tok.type) {
        case TokenType::EQ: return AssignOp::Assign;
        case TokenType::PLUS_EQ: return AssignOp::AddEq;
        case TokenType::MINUS_EQ: return AssignOp::SubEq;
        case TokenType::STAR_EQ: return AssignOp::MulEq;
        case TokenType::SLASH_EQ: return AssignOp::DivEq;
        case TokenType::PERCENT_EQ: return AssignOp::ModEq;
        default: return AssignOp::NullAssign;
    }
}

std::unique_ptr<Expr> parseExpr(ParseState& ps, int minPrecedence = 0);

// Primary Expr Parser
std::unique_ptr<Expr> parsePrimary(ParseState& ps) {
    const Token& tok = ps.peek();
    auto expr = std::make_unique<Expr>();

    if(tok.type == TokenType::NUMBER || tok.type == TokenType::FLOAT ||
       tok.type == TokenType::HEXNUMBER || tok.type == TokenType::BINARYNUMBER ||
       tok.type == TokenType::CHAR_LITERAL || tok.type == TokenType::STRING ||
       tok.type == TokenType::RAW_STRING || tok.type == TokenType::KW_TRUE ||
       tok.type == TokenType::KW_FALSE || tok.type == TokenType::KW_NULL) {
        expr->kind = Expr::Kind::Literal;
        expr->literal = std::make_unique<Literal>();
        expr->literal->value = tok.value;
        switch(tok.type) {
            case TokenType::NUMBER: expr->literal->kind = Literal::Kind::Number; break;
            case TokenType::FLOAT: expr->literal->kind = Literal::Kind::Float; break;
            case TokenType::HEXNUMBER: expr->literal->kind = Literal::Kind::Hex; break;
            case TokenType::BINARYNUMBER: expr->literal->kind = Literal::Kind::Binary; break;
            case TokenType::CHAR_LITERAL: expr->literal->kind = Literal::Kind::Char; break;
            case TokenType::STRING: expr->literal->kind = Literal::Kind::String; break;
            case TokenType::RAW_STRING: expr->literal->kind = Literal::Kind::RawString; break;
            case TokenType::KW_TRUE: expr->literal->kind = Literal::Kind::True; break;
            case TokenType::KW_FALSE: expr->literal->kind = Literal::Kind::False; break;
            case TokenType::KW_NULL: expr->literal->kind = Literal::Kind::Null; break;
            default: break;
        }
        ps.advance();
        return expr;
    }

    if(tok.type == TokenType::IDENT) {
        expr->kind = Expr::Kind::Ident;
        expr->ident = std::make_unique<QualifiedName>();
        expr->ident->parts.push_back(tok.value);
        ps.advance();
        return expr;
    }

    if(tok.type == TokenType::LPAREN) {
        ps.advance();
        expr = parseExpr(ps);
        if(!ps.match(TokenType::RPAREN))
            ps.error("expected ')'");
        return expr;
    }

    if(tok.type == TokenType::KW_SIZEOF || tok.type == TokenType::KW_ALIGNOF) {
        bool isSizeOf = tok.type == TokenType::KW_SIZEOF;
        ps.advance();
        if(!ps.match(TokenType::LPAREN)) ps.error("expected '(' after sizeof/alignof");
        expr = std::make_unique<Expr>();
        if(isSizeOf) expr->kind = Expr::Kind::SizeOf;
        else expr->kind = Expr::Kind::AlignOf;
        expr->sizeof_type = std::make_unique<Type>();
        if(ps.peek().type == TokenType::IDENT) {
            expr->sizeof_type->base = std::make_unique<QualifiedName>();
            expr->sizeof_type->base->parts.push_back(ps.advance().value);
        }
        if(!ps.match(TokenType::RPAREN)) ps.error("expected ')' after sizeof/alignof type");
        return expr;
    }

    if(tok.type == TokenType::KW_NEW) {
        ps.advance();
        expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::New;
        if(ps.peek().type == TokenType::IDENT) {
            expr->new_expr.type = std::make_unique<QualifiedName>();
            expr->new_expr.type->parts.push_back(ps.advance().value);
        } else {
            ps.error("expected type after 'new'");
        }
        if(ps.match(TokenType::LPAREN)) {
            while(ps.peek().type != TokenType::RPAREN) {
                expr->new_expr.args.push_back(parseExpr(ps));
                if(!ps.match(TokenType::COMMA)) break;
            }
            if(!ps.match(TokenType::RPAREN)) ps.error("expected ')' after new args");
        }
        return expr;
    }

    if(tok.type == TokenType::MINUS || tok.type == TokenType::STAR ||
       tok.type == TokenType::AMP || tok.type == TokenType::BANG ||
       tok.type == TokenType::TILDE) {
        expr->kind = Expr::Kind::Unary;
        expr->unary.op = tokenToUnaryOp(tok);
        ps.advance();
        expr->unary.expr = parseExpr(ps, 100);
        return expr;
    }

    ps.error("unexpected token in expression");
    return nullptr;
}
