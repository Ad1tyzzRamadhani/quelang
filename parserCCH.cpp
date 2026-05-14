#include "ast.hpp"
#include "lexer.hpp"

#include <memory>
#include <stdexcept>
#include <sstream>

class ParserCCH {
public:
    explicit ParserCCH(std::vector<Token> toks)
        : tokens(std::move(toks)) {}

    std::unique_ptr<FileProgram> parse() {
        auto file = std::make_unique<FileProgram>();

        while (!is(TokenType::EOF_TOKEN)) {
            file->decls.push_back(parseTopLevel());
        }

        return file;
    }

private:
    std::vector<Token> tokens;
    size_t pos = 0;
    const Token& peek(int o = 0) const {
        if (pos + o >= tokens.size())
            return tokens.back();
        return tokens[pos + o];
    }

    bool is(TokenType t, int o = 0) const {
        return peek(o).type == t;
    }

    const Token& advance() {
        return tokens[pos++];
    }

    bool match(TokenType t) {
        if (is(t)) {
            advance();
            return true;
        }
        return false;
    }

    [[noreturn]] void error(const std::string& msg) {
        const auto& tk = peek();

        throw std::runtime_error(
            tk.file + ":" +
            std::to_string(tk.line) + ":" +
            std::to_string(tk.column) + " " + msg
        );
    }

    const Token& expect(TokenType t, const std::string& msg) {
        if (!is(t))
            error(msg);
        return advance();
    }

    Visibility parseVisibility() {
        if (match(TokenType::KW_PUBLIC))
            return Visibility::Public;

        return Visibility::Private;
    }

    std::unique_ptr<Node> parseTopLevel() {

        Visibility vis = Visibility::Private;

        if (is(TokenType::KW_PUBLIC))
            vis = parseVisibility();

        // struct forward
        if (is(TokenType::KW_STRUCT)) {
            return parseStructForward(vis);
        }

        // class forward
        if (is(TokenType::KW_CLASS)) {
            return parseClassForward(vis);
        }

        // enum
        if (is(TokenType::KW_ENUM)) {
            return parseEnum(vis);
        }

        // variable/function
        return parseVarOrFunction(vis);
    }

    std::unique_ptr<QualifiedName> parseQualifiedName() {
        auto q = std::make_unique<QualifiedName>();

        auto tk = expect(TokenType::IDENT, "expected identifier");
        q->parts.push_back(tk.value);

        while (match(TokenType::SCOPE)) {
            auto p = expect(TokenType::IDENT, "expected identifier");
            q->parts.push_back(p.value);
        }

        return q;
    }

    std::unique_ptr<Type> parseType() {

        auto ty = std::make_unique<Type>();

        // qualifiers

        while (true) {

            if (match(TokenType::KW_CONST)) {
                ty->qualifiers.push_back(TypeQualifier::Const);
                continue;
            }

            if (match(TokenType::KW_VOLATILE)) {
                ty->qualifiers.push_back(TypeQualifier::Volatile);
                continue;
            }

            break;
        }

        ty->base = parseQualifiedName();

        // modifiers

        while (true) {

            if (match(TokenType::STAR)) {

                TypeModifier mod;
                mod.kind = TypeModifier::Kind::Pointer;

                ty->modifiers.push_back(std::move(mod));
                continue;
            }

            if (match(TokenType::AMP)) {

                TypeModifier mod;
                mod.kind = TypeModifier::Kind::Reference;

                ty->modifiers.push_back(std::move(mod));
                continue;
            }

            if (match(TokenType::LBRACKET)) {

                TypeModifier mod;
                mod.kind = TypeModifier::Kind::Array;

                if (!is(TokenType::RBRACKET)) {
                    mod.array_dims.push_back(parseDummyExpr());
                }

                expect(TokenType::RBRACKET, "expected ']'");
                ty->modifiers.push_back(std::move(mod));
                continue;
            }

            // funcptr

            if (match(TokenType::LPAREN)) {

                TypeModifier mod;
                mod.kind = TypeModifier::Kind::FuncPtr;

                if (!is(TokenType::RPAREN)) {

                    while (true) {
                        mod.func_params.push_back(parseType());

                        if (!match(TokenType::COMMA))
                            break;
                    }
                }

                expect(TokenType::RPAREN, "expected ')'");

                if (!(match(TokenType::STAR) || match(TokenType::AMP)))
                    error("expected '*' or '&' after funcptr");

                ty->modifiers.push_back(std::move(mod));
                continue;
            }

            break;
        }

        return ty;
    }

    std::unique_ptr<Expr> parseDummyExpr() {

        auto e = std::make_unique<Expr>();

        e->kind = Expr::Kind::Literal;
        e->literal = std::make_unique<Literal>();

        auto tk = advance();

        switch (tk.type) {

            case TokenType::NUMBER:
                e->literal->kind = Literal::Kind::Number;
                break;

            case TokenType::HEXNUMBER:
                e->literal->kind = Literal::Kind::Hex;
                break;

            case TokenType::BINARYNUMBER:
                e->literal->kind = Literal::Kind::Binary;
                break;

            case TokenType::FLOAT:
                e->literal->kind = Literal::Kind::Float;
                break;

            default:
                error("expected literal");
        }

        e->literal->value = tk.value;

        return e;
    }

    std::unique_ptr<Node> parseStructForward(Visibility vis) {

        advance();

        auto f = std::make_unique<ForwardDecl>();

        f->visibility = vis;
        f->kind = ForwardDecl::Kind::Struct;

        f->name = parseQualifiedName();

        expect(TokenType::SEMICOLON, "expected ';'");

        return f;
    }

    std::unique_ptr<Node> parseClassForward(Visibility vis) {

        advance();

        auto f = std::make_unique<ForwardDecl>();

        f->visibility = vis;
        f->kind = ForwardDecl::Kind::Class;

        f->name = parseQualifiedName();

        expect(TokenType::SEMICOLON, "expected ';'");

        return f;
    }

    std::unique_ptr<Node> parseEnum(Visibility vis) {

        expect(TokenType::KW_ENUM, "expected enum");

        auto en = std::make_unique<EnumDef>();

        en->visibility = vis;
        en->name = parseQualifiedName();

        // underlying type

        if (match(TokenType::COLON)) {
            en->underlying_type = parseType();
        }

        // opaque enum

        if (match(TokenType::SEMICOLON)) {
            return en;
        }

        expect(TokenType::LBRACE, "expected '{'");

        while (!is(TokenType::RBRACE)) {

            EnumDef::Item item;

            auto name =
                expect(TokenType::IDENT, "expected enum item");

            item.name = name.value;

            if (match(TokenType::EQ)) {
                item.value = parseDummyExpr();
            }

            en->items.push_back(std::move(item));

            if (!match(TokenType::COMMA))
                break;
        }

        expect(TokenType::RBRACE, "expected '}'");

        return en;
    }

    std::unique_ptr<Node> parseVarOrFunction(Visibility vis) {

        bool is_static = false;
        bool is_uniform = false;
        bool is_virtual = false;

        while (true) {

            if (match(TokenType::KW_STATIC)) {
                is_static = true;
                continue;
            }

            if (match(TokenType::KW_UNIFORM)) {
                is_uniform = true;
                continue;
            }

            if (match(TokenType::KW_VIRTUAL)) {
                is_virtual = true;
                continue;
            }

            break;
        }

        auto type = parseType();

        auto name =
            expect(TokenType::IDENT, "expected identifier");

        // function

        if (match(TokenType::LPAREN)) {

            auto fn = std::make_unique<Function>();

            fn->visibility = vis;
            fn->return_type = std::move(type);

            fn->name = std::make_unique<QualifiedName>();
            fn->name->parts.push_back(name.value);

            fn->is_static = is_static;
            fn->is_virtual = is_virtual;

            if (!is(TokenType::RPAREN)) {

                while (true) {

                    Function::Param p;

                    p.type = parseType();

                    if (is(TokenType::IDENT)) {
                        p.name = advance().value;
                    }

                    fn->params.push_back(std::move(p));

                    if (!match(TokenType::COMMA))
                        break;
                }
            }

            expect(TokenType::RPAREN, "expected ')'");

            expect(TokenType::SEMICOLON, "expected ';'");

            return fn;
        }

        // variable

        auto stmt = std::make_unique<Stmt>();

        stmt->kind = Stmt::Kind::VarDecl;

        stmt->var_decl.type = std::move(type);

        if (is_static)
            stmt->var_decl.storage = StorageKind::Static;

        if (is_uniform)
            stmt->var_decl.storage = StorageKind::Uniform;

        Stmt::var_decl::Item item;

        item.name = std::make_unique<QualifiedName>();
        item.name->parts.push_back(name.value);

        // slot

        if (match(TokenType::COLON)) {

            auto lit = advance();

            if (
                lit.type != TokenType::NUMBER &&
                lit.type != TokenType::HEXNUMBER &&
                lit.type != TokenType::BINARYNUMBER
            ) {
                error("expected slot literal");
            }

            stmt->var_decl.slot_id =
                std::stoi(lit.value, nullptr, 0);
        }

        stmt->var_decl.items.push_back(std::move(item));

        expect(TokenType::SEMICOLON, "expected ';'");

        return stmt;
    }
};
