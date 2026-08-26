#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

struct Expr;
struct Stmt;
struct Type;

// AST Base / Node Base

struct Node {
    virtual ~Node() = default;
    int line_start = -1, column_start = -1;
    int line_end = -1, column_end = -1;
    std::string file;
};

// Qualified Name System

struct QualifiedName : Node {
    std::vector<std::string> parts;
};

// Literal

struct Literal : Node {
    enum class Kind {
        Number, Float, Hex, Binary,
        Char, String, RawString,
        True, False, Null, None
    } kind;

    std::string value;
};

// Type System

enum class TypeQualifier { Const, Volatile, Atomic};

struct TypeModifier {
    enum class Kind { Pointer, Reference, Restrict , FuncPtr } kind;
    std::vector<std::unique_ptr<Type>> func_params;
    bool is_coroutine = false;
    std::unique_ptr<Type> func_return;
    std::unique_ptr<Type> throws_type;
};

struct Type : Node {
    std::vector<TypeQualifier> qualifiers;
    std::unique_ptr<QualifiedName> base;
    std::vector<TypeModifier> modifiers;
};

// Expr System

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Lte, Gt, Gte,
    And, Or, Xor,
    BitAnd, BitOr, BitXor,
    Shl, Shr,
    Coalesce,
    In
};

enum class UnaryOp {
    Neg, Deref, Ref, Not, BitNot
};

enum class AssignOp {
    Assign, AddEq, SubEq, MulEq, DivEq, ModEq
};

struct Expr : Node {
    enum class Kind {
        Literal, Ident,

        Unary,
        Binary,
        Assign,
        Ternary,
        Postfix,

        Cast,
        New,
        Move,

        StructInit,
        ArrayLiteral
    } kind;

    // BasicExpr
    std::unique_ptr<Literal> literal;
    std::unique_ptr<QualifiedName> ident;

    // UnaryOp
    struct {
        UnaryOp op;
        std::unique_ptr<Expr> expr;
    } unary;

    // BinaryOp
    struct {
        BinaryOp op;
        std::unique_ptr<Expr> lhs;
        std::unique_ptr<Expr> rhs;
    } binary;

    // Single Assign Expr -> a = add(5,7);
    struct {
        AssignOp op;
        std::unique_ptr<Expr> lhs;
        std::unique_ptr<Expr> rhs;
    } assign;

    // Ternary
    struct {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> then_expr;
        std::unique_ptr<Expr> else_expr;
    } ternary;

    // PostFix Op
    struct PostfixOp {
        enum class Kind { AtomicRef, MemberAccess, StaticAccess, SafeArrow, Scope, Arrow, Call, Index } kind;

        std::string name;
        std::vector<std::unique_ptr<Expr>> args;
        std::unique_ptr<Expr> index;
    };

    struct {
        std::unique_ptr<Expr> base;
        std::vector<PostfixOp> ops;
    } postfix;

    // cast -> foo as! T;
    struct {
        std::unique_ptr<Expr> base;
        std::unique_ptr<Type> target;
    } cast;

    // new
    struct {
        std::unique_ptr<QualifiedName> type;
        std::vector<std::unique_ptr<Expr>> args;
        std::vector<std::unique_ptr<Expr>> array_dims;
    } new_expr;

    struct {
        std::unique_ptr<Expr> value;
    } move_expr;

    struct {
        std::vector<std::pair<std::optional<std::string>, std::unique_ptr<Expr>>> fields;
    } struct_init;

    std::vector<std::unique_ptr<Expr>> array_items;
};

// Statement

struct UseDecl;
struct Constructor;
enum class Visibility { Private, Public };

struct VarDecl : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<Type> type;
    struct Item {
        std::string name;
        std::unique_ptr<Expr> init;
        std::vector<std::unique_ptr<Expr>> args;
        std::unique_ptr<Literal> bit_width;
        std::vector<std::unique_ptr<Literal>> array_dims;
    };

    std::vector<Item> items;
    bool is_atomic = false;
    bool is_static = false;
    bool is_extern = false;
    bool is_none = false;
};

struct Stmt : Node {
    enum class Kind {
        VarDecl, If, While, DoStmt, For, Resume,
        Return, Break, Continue, SwitchCase,
        ExprStmt, Block, Defer, UseStmt, ThrowStmt,
        Label, Jump, Await, Unsafe,
        Drop, Wipe, Yield
    } kind;

    struct {
        std::vector<std::unique_ptr<Stmt>> stmts;
    } block;

    VarDecl var_decl;

    struct IfStmt {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Stmt> then_block;

        struct Elif {
            std::unique_ptr<Expr> cond;
            std::unique_ptr<Stmt> block;
        };

        std::vector<Elif> elifs;
        std::unique_ptr<Stmt> else_block;
    } if_stmt;

    struct {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Stmt> body;
    } while_stmt;

    struct {
        std::unique_ptr<Stmt> body;
        std::unique_ptr<Expr> cond;

        bool iscatch = false;
        std::unique_ptr<VarDecl> error_var;
        std::unique_ptr<Stmt> catch_body;
    } do_stmt;

    struct {
        VarDecl init;
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> update;
        std::unique_ptr<Stmt> body;
    } for_stmt;

    struct SwitchStmt {
        std::unique_ptr<Expr> expr;

        struct Case {
            bool is_default = false;
            std::vector<std::unique_ptr<Literal>> values;
            std::unique_ptr<Stmt> body;
        };

        std::vector<Case> cases;
    } switch_stmt;

    std::unique_ptr<Expr> expr_stmt;

    struct {
        std::unique_ptr<Expr> expr;
        bool is_array = false;
        bool is_reference = false;
    } drop_stmt;

    struct {
        std::unique_ptr<Expr> expr;
        bool is_array = false;
    } wipe_stmt;

    struct {
        std::unique_ptr<Expr> expr;
    } throw_stmt;

    struct {
        std::unique_ptr<Stmt> stmt;
    } defer_stmt;

    struct {
        std::unique_ptr<Expr> target;
    } resume_stmt;

    struct {
        std::unique_ptr<Expr> target;
    } await_stmt;

    struct {
        std::unique_ptr<Stmt> stmt;
    } unsafe_stmt;

    std::string label;
    std::string jump_target;
    std::unique_ptr<UseDecl> use_stmt;

    std::unique_ptr<Expr> return_values;
};

// Top Level Scope Program

struct ForwardDecl : Node {
    Visibility visibility = Visibility::Private;
    enum class Kind { Struct, Function } kind;
    std::unique_ptr<QualifiedName> name;
    std::unique_ptr<Type> return_types;
    std::vector<std::unique_ptr<Type>> params;
    bool is_static = false;
};

struct Function : Node {
    Visibility visibility = Visibility::Private;

    std::unique_ptr<Type> return_types;
    std::unique_ptr<QualifiedName> name;

    struct Param {
        std::unique_ptr<Type> type;
        std::string name;
        std::unique_ptr<Expr> init;
    };
    std::vector<Param> params;

    std::unique_ptr<Stmt> body;
    std::unique_ptr<Type> throws_type;

    bool is_coroutine = false;
    bool is_static = false; // static void foo();
    bool is_const = false; // void foo() const;
    bool is_extern = false; // extern void foo();
    bool is_noreturn = false;
};

// Data Structure

struct UnionDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;

    struct Field {
        std::unique_ptr<Type> type;
        std::string name;
    };

    std::vector<Field> fields;
    std::optional<std::string> default_field;
};

struct Constructor : Node {
    struct Param {
        std::unique_ptr<Type> type;
        std::string name;
        std::unique_ptr<Expr> init;
    };
    std::vector<Param> params;
    std::unique_ptr<Stmt> body;
};

struct Destructor : Node {
    std::unique_ptr<Stmt> body;
};

struct StructDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> members;
    std::unique_ptr<Constructor> ctor;
    std::unique_ptr<Destructor> dtor;
    std::optional<std::string> default_field;
};

// Enum ( Enum Union & Constant Enum )

struct EnumDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;
    std::unique_ptr<Type> underlying_type;

    struct Item {
        std::string name;
        std::unique_ptr<Expr> value;
    };

    std::vector<Item> items;
};

// Module & Program

struct UseDecl : Node {
    std::unique_ptr<QualifiedName> target;
    std::optional<std::string> alias;
};

struct NamespaceDecl : Node {
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> items;
};

struct FileProgram : Node {
    std::vector<std::unique_ptr<Node>> decls;
};
