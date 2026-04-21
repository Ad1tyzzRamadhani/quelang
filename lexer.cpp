#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <stdexcept>

enum class TokenType {
    EOF_TOKEN,

    IDENT,
    NUMBER,
    FLOAT,
    HEXNUMBER,
    BINARYNUMBER,
    STRING,
    RAW_STRING,
    CHAR_LITERAL,
    ARROW,

    KW_IF, KW_ELIF, KW_ELSE,
    KW_WHILE, KW_DO, KW_FOR,
    KW_RETURN, KW_BREAK, KW_CONTINUE,
    KW_STRUCT, KW_ENUM, KW_PACKAGE, KW_IN,
    KW_USE, KW_AS, KW_DROP, KW_CLASS, KW_UNION,
    KW_TRUE, KW_FALSE, KW_NULL, KW_THIS, KW_VIRTUAL,
    KW_PUBLIC, KW_STATIC, KW_CONST, KW_VOLATILE, KW_EXTEND,
    KW_AND, KW_OR, KW_XOR, KW_OVERRIDE, KW_PROTECT,
    KW_ALIGNOF, KW_SWITCH, KW_EXTERN,
    KW_CASE, KW_DEFAULT, KW_SUPER, KW_CONSTRUCT,
    KW_ALIGN, KW_JUMPTO, KW_MATCH,
    KW_NEW, KW_DELETE, KW_NOT,

    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, EQEQ, NEQ, SCOPE,
    LT, LTE, GT, GTE, RANGE,
    PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ, PERCENT_EQ,
    AMP,PIPE, CARET, TILDE, PIPE,
    LSHIFT, RSHIFT,
    QUESTION, QUESTION_EQ, COALESCE,
    BANG, DOLLAR,
    LPAREN, RPAREN,
    LBRACE, RBRACE,
    LBRACKET, RBRACKET,
    COMMA, SEMICOLON, DOT, COLON, SAFE_DOT
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    std::string file;
};

class Lexer {
public:
    explicit Lexer(const std::string& src)
        : source(src) {}

    std::vector<Token> tokenize();

private:
    const std::string& source;
    size_t pos = 0;
    int line = 1;
    int column = 1;
    std::string current_file = "<unknown>";
    std::vector<Token> tokens;

    static const std::unordered_map<std::string, TokenType> keyword_map;

    char peek(int o = 0) const {
        if (pos + o >= source.size()) return '\0';
        return source[pos + o];
    }

    char advance() {
        char c = source[pos++];
        if (c == '\n') { line++; column = 1; }
        else column++;
        return c;
    }

    void emit(TokenType t, const std::string& v, int l, int c) {
        tokens.push_back({t, v, l, c, current_file});
    }

    [[noreturn]] void error(const std::string& m) {
        throw std::runtime_error(
            current_file + ":" +
            std::to_string(line) + ":" +
            std::to_string(column) + " " + m
        );
    }

    void skipTrivia() {
        for (;;) {
            while (std::isspace((unsigned char)peek())) advance();

            if (peek() == '#') {
                if (peek(1) == '#') {
                    advance(); advance();
                    while (peek() && !(peek()=='#' && peek(1)=='#'))
                        advance();
                    if (peek()) { advance(); advance(); }
                } else {
                    while (peek() && peek()!='\n')
                        advance();
                }
                continue;
            }
            break;
        }
    }

    void identOrKeyword() {
        int l = line, c = column;
        std::string s;
        while (std::isalnum((unsigned char)peek()) || peek()=='_')
            s += advance();

        auto it = keyword_map.find(s);
        if (it != keyword_map.end())
            emit(it->second, "", l, c);
        else
            emit(TokenType::IDENT, s, l, c);
    }

    void number() {
        int l=line,c=column;
        std::string s;

        while (std::isdigit((unsigned char)peek()) || peek()=='_') {
            if (peek()!='_') s+=advance();
            else advance();
        }

        bool isFloat = false;

        if (peek() == '.' && peek(1) != '.') {
            isFloat = true;
            s += advance();
            while (std::isdigit((unsigned char)peek()) || peek()=='_') {
                if (peek()!='_') s+=advance();
                else advance();
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            isFloat = true;
            s += advance();
            if (peek()=='+'||peek()=='-') s+=advance();
            if (!std::isdigit((unsigned char)peek()))
                error("invalid float exponent");
            while (std::isdigit((unsigned char)peek())||peek()=='_') {
                if (peek()!='_') s+=advance();
                else advance();
            }
        }

        if (isFloat)
            emit(TokenType::FLOAT,s,l,c);
        else
            emit(TokenType::NUMBER,s,l,c);
    }

    void hex() {
        int l=line,c=column;
        std::string s;
        s+=advance(); s+=advance();
        while (std::isxdigit((unsigned char)peek()) || peek()=='_') {
            if (peek()!='_') s+=advance();
            else advance();
        }
        emit(TokenType::HEXNUMBER,s,l,c);
    }

    void binary() {
        int l=line,c=column;
        std::string s;
        s+=advance(); s+=advance();
        while (peek()=='0'||peek()=='1'||peek()=='_') {
            if (peek()!='_') s+=advance();
            else advance();
        }
        emit(TokenType::BINARYNUMBER,s,l,c);
    }

    void string() {
        int l=line,c=column;
        advance();
        std::string v;

        while (peek() && peek()!='"') {

            if (peek()=='\\') {
                advance();
                char e=advance();
                switch(e){
                    case 'n':v+='\n';break;
                    case 't':v+='\t';break;
                    case '0':v+='\0';break;
                    default:v+=e;
                }
            } else v+=advance();
        }

        if (advance()!='"') error("unterminated string");
        emit(TokenType::STRING,v,l,c);
    }

    void rawString() {
        int l=line,c=column;
        advance(); // r
        advance(); // "

        std::string delim;
        while (peek() && peek()!='(')
            delim+=advance();
        if (peek()!='(') error("invalid raw string");
        advance();

        std::string content;

        while (true) {
            if (!peek()) error("unterminated raw string");

            if (peek()==')') {
                size_t save=pos;
                advance();
                bool match=true;
                for(char d:delim){
                    if(peek()!=d){match=false;break;}
                    advance();
                }
                if(match && peek()=='"'){
                    advance();
                    break;
                }
                pos=save;
            }
            content+=advance();
        }

        emit(TokenType::RAW_STRING,content,l,c);
    }

    void character() {
        int l=line,c=column;
        advance();
        char v;
        if (peek()=='\\') {
            advance();
            char e=advance();
            v=(e=='n'?'\n':e=='t'?'\t':e);
        } else v=advance();
        if (advance()!='\'') error("invalid char");
        emit(TokenType::CHAR_LITERAL,std::string(1,v),l,c);
    }

    void op() {
        int l=line,c=column;

        char a=peek(), b=peek(1);

        if(a=='-'&&b=='>'){advance();advance();emit(TokenType::ARROW,"",l,c);return;}
        if(a=='+'&&b=='='){advance();advance();emit(TokenType::PLUS_EQ,"",l,c);return;}
        if(a=='-'&&b=='='){advance();advance();emit(TokenType::MINUS_EQ,"",l,c);return;}
        if(a=='*'&&b=='='){advance();advance();emit(TokenType::STAR_EQ,"",l,c);return;}
        if(a=='/'&&b=='='){advance();advance();emit(TokenType::SLASH_EQ,"",l,c);return;}
        if(a=='%'&&b=='='){advance();advance();emit(TokenType::PERCENT_EQ,"",l,c);return;}
        if(a=='='&&b=='='){advance();advance();emit(TokenType::EQEQ,"",l,c);return;}
        if(a=='!'&&b=='='){advance();advance();emit(TokenType::NEQ,"",l,c);return;}
        if(a=='<'&&b=='='){advance();advance();emit(TokenType::LTE,"",l,c);return;}
        if(a=='>'&&b=='='){advance();advance();emit(TokenType::GTE,"",l,c);return;}
        if(a=='?'&&b=='.'){advance();advance();emit(TokenType::SAFE_DOT,"",l,c);return;}
        if(a=='?'&&b=='='){advance();advance();emit(TokenType::QUESTION_EQ,"",l,c);return;}
        if(a==':'&&b==':'){advance();advance();emit(TokenType::SCOPE,"",l,c);return;}
        if(a=='<'&&b=='<'){advance();advance();emit(TokenType::LSHIFT,"",l,c);return;}
        if(a=='>'&&b=='>'){advance();advance();emit(TokenType::RSHIFT,"",l,c);return;}
        if(a=='?'&&b=='?'){advance();advance();emit(TokenType::COALESCE,"",l,c);return;}
        if(a=='.'&&b=='.'){advance();advance();emit(TokenType::RANGE,"",l,c);return;}
        if(a=='|'&&b=='>'){advance();advance();emit(TokenType::PIPE,"",l,c);return;}

        char ch=advance();

        switch(ch){
            case '+':emit(TokenType::PLUS,"",l,c);break;
            case '-':emit(TokenType::MINUS,"",l,c);break;
            case '*':emit(TokenType::STAR,"",l,c);break;
            case '/':emit(TokenType::SLASH,"",l,c);break;
            case '%':emit(TokenType::PERCENT,"",l,c);break;
            case '=':emit(TokenType::EQ,"",l,c);break;
            case '<':emit(TokenType::LT,"",l,c);break;
            case '>':emit(TokenType::GT,"",l,c);break;
            case '&':emit(TokenType::AMP,"",l,c);break;
            case '?':emit(TokenType::QUESTION,"",l,c);break;
            case '!':emit(TokenType::BANG,"",l,c);break;
            case '$':emit(TokenType::DOLLAR,"",l,c);break;
            case '(':emit(TokenType::LPAREN,"",l,c);break;
            case ')':emit(TokenType::RPAREN,"",l,c);break;
            case '{':emit(TokenType::LBRACE,"",l,c);break;
            case '}':emit(TokenType::RBRACE,"",l,c);break;
            case '[':emit(TokenType::LBRACKET,"",l,c);break;
            case ']':emit(TokenType::RBRACKET,"",l,c);break;
            case ',':emit(TokenType::COMMA,"",l,c);break;
            case ';':emit(TokenType::SEMICOLON,"",l,c);break;
            case '.':emit(TokenType::DOT,"",l,c);break;
            case ':':emit(TokenType::COLON,"",l,c);break;
            case '|': emit(TokenType::PIPE,"",l,c); break;
            case '^': emit(TokenType::CARET,"",l,c); break;
            case '~': emit(TokenType::TILDE,"",l,c); break;
            default:error("unknown character");
        }
    }

    void next() {
        skipTrivia();
        char c=peek();
        if(!c) return;

        if(c=='r' && peek(1)=='"'){ rawString(); return; }
        if(std::isalpha((unsigned char)c)||c=='_'){ identOrKeyword(); return; }
        if(c=='0'&&(peek(1)=='x'||peek(1)=='X')){ hex(); return; }
        if(c=='0'&&(peek(1)=='b'||peek(1)=='B')){ binary(); return; }
        if(std::isdigit((unsigned char)c)){ number(); return; }
        if(c=='"'){ string(); return; }
        if(c=='\''){ character(); return; }
        op();
    }
};

const std::unordered_map<std::string,TokenType> Lexer::keyword_map={
    {"if",TokenType::KW_IF},{"elif",TokenType::KW_ELIF},{"else",TokenType::KW_ELSE},
    {"while",TokenType::KW_WHILE},{"do",TokenType::KW_DO},{"for",TokenType::KW_FOR},
    {"return",TokenType::KW_RETURN},{"break",TokenType::KW_BREAK},{"union",TokenType::KW_UNION},
    {"continue",TokenType::KW_CONTINUE},{"extern",TokenType::KW_EXTERN},
    {"struct",TokenType::KW_STRUCT},{"enum",TokenType::KW_ENUM},{"drop",TokenType::KW_DROP},
    {"package",TokenType::KW_PACKAGE},{"use",TokenType::KW_USE},{"as",TokenType::KW_AS},
    {"true",TokenType::KW_TRUE},{"false",TokenType::KW_FALSE},{"null",TokenType::KW_NULL},
    {"public",TokenType::KW_PUBLIC},{"static",TokenType::KW_STATIC},{"this",TokenType::KW_THIS},
    {"const",TokenType::KW_CONST},{"volatile",TokenType::KW_VOLATILE},{"extend",TokenType::KW_EXTEND},
    {"and",TokenType::KW_AND},{"or",TokenType::KW_OR},{"xor",TokenType::KW_XOR},
    {"switch",TokenType::KW_SWITCH},{"case",TokenType::KW_CASE},{"override",TokenType::KW_OVERRIDE},
    {"default",TokenType::KW_DEFAULT},{"delete",TokenType::KW_DELETE},{"virtual",TokenType::KW_VIRTUAL},
    {"not", TokenType::KW_NOT},{"in",TokenType::KW_IN},{"match",TokenType::KW_MATCH},
    {"jumpto",TokenType::KW_JUMPTO},{"super",TokenType::KW_SUPER},
    {"alignof",TokenType::KW_ALIGNOF},
    {"new",TokenType::KW_NEW},{"protect",TokenType::KW_PROTECT},
    {"align",TokenType::KW_ALIGN},{"construct",TokenType::KW_CONSTRUCT}
};

std::vector<Token> Lexer::tokenize() {
    while(peek()) next();
    emit(TokenType::EOF_TOKEN,"",line,column);
    return tokens;
}
