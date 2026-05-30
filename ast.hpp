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
        True, False, Null
    } kind;

    std::string value;
};

// Type System

enum class TypeQualifier { Const, Volatile };

struct TypeModifier {
    enum class Kind { Pointer, Reference, FuncPtr } kind;
    std::vector<std::unique_ptr<Type>> func_params;
    std::vector<std::unique_ptr<Type>> func_return;
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
    Assign, AddEq, SubEq, MulEq, DivEq, ModEq, NullAssign
};

struct Expr : Node {
    enum class Kind {
        Literal, Ident,

        Pipe,
        Unary,
        Binary,
        Assign,
        MultiAssign,
        Ternary,
        Postfix,

        Call,
        Index,

        UnsafeCast,
        New,

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
    } single_assign;

    // Multiple Assign Expr -> a, b = foo();
    struct {
        std::vector<std::unique_ptr<Expr>> lhs;
        std::vector<std::unique_ptr<Expr>> rhs;
    } multi_assign;

    // Ternary
    struct {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> then_expr;
        std::unique_ptr<Expr> else_expr;
    } ternary;

    // Pipe Expr -> 9 |> count;
    struct {
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
    } pipe;

    // PostFix Op
    struct PostfixOp {
        enum class Kind { Field, NullField, Scope, Arrow, Call, Index } kind;

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
    } unsafe_cast;

    // new
    struct {
        std::unique_ptr<QualifiedName> type;
        std::vector<std::unique_ptr<Expr>> args;
        std::vector<std::unique_ptr<Expr>> array_dims;
    } new_expr;

    struct {
        std::vector<std::pair<std::optional<std::string>, std::unique_ptr<Expr>>> fields;
    } struct_init;

    std::vector<std::unique_ptr<Expr>> array_items;
};

// Statement

struct UseDecl;
enum class StorageKind {
    Normal,
    Static,
    Uniform
};

struct Stmt : Node {
    enum class Kind {
        VarDecl, If, While, DoWhile, For, Resume,
        Return, Break, Continue, SwitchCase,
        ExprStmt, Block, Defer, UseStmt,
        Label, Jump, InlineStruct,
        Drop, Yield
    } kind;

    struct {
        std::vector<std::unique_ptr<Stmt>> stmts;
    } block;

    struct VarDecl {
        std::unique_ptr<Type> type;
        StorageKind storage = StorageKind::Normal;

        struct Item {
            std::unique_ptr<QualifiedName> name;
            std::unique_ptr<Expr> init;
            std::unique_ptr<Expr> ctor;
            std::vector<std::unique_ptr<Expr>> array_dims;
        };

        std::vector<Item> items;
        std::optional<int> slot_id;
        bool is_static = false;
        bool is_extern = false;
    } var_decl;

    struct {
        std::vector<std::unique_ptr<Node>> members;
        std::unique_ptr<Constructor> ctor;
        std::unique_ptr<QualifiedName> var_name;
        std::unique_ptr<Expr> init_expr;
        std::vector<TypeQualifier> qualifiers;
        std::vector<TypeModifier> modifiers;
    } inline_struct;

    struct {
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
    } do_while_stmt;

    struct {
        std::unique_ptr<Stmt> init;
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> update;
        std::unique_ptr<Stmt> body;
    } for_stmt;

    struct {
        std::unique_ptr<Expr> expr;

        struct Case {
            bool is_default = false;
            std::vector<std::unique_ptr<Expr>> values;
            std::unique_ptr<Stmt> body;
        };

        std::vector<Case> cases;
    } switch_stmt;

    std::unique_ptr<Expr> expr_stmt;

    struct {
        std::unique_ptr<Expr> expr;
    } drop_stmt;

    struct {
        std::unique_ptr<Stmt> stmt;
    } defer_stmt;

    struct {
        std::unique_ptr<Expr> target;
    } resume_stmt;

    std::string label;
    std::string jump_target;
    std::unique_ptr<UseDecl> use_stmt;

    std::vector<std::unique_ptr<Expr>> ret_values;
};

// Top Level Scope Program

enum class Visibility { Private, Public, Protect };

struct ForwardDecl : Node {
    Visibility visibility = Visibility::Private;
    enum class Kind { Struct, Class, Function } kind;
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Type>> return_types;
    std::vector<std::unique_ptr<Type>> params;
    bool is_static = false;
    bool is_virtual = false;
};

struct Function : Node {
    Visibility visibility = Visibility::Private;

    std::vector<std::unique_ptr<Type>> return_types;
    std::unique_ptr<QualifiedName> name;

    struct Param {
        std::unique_ptr<Type> type;
        std::string name;
        std::unique_ptr<Expr> init;
    };

    struct CoroutineInfo {
        std::unique_ptr<QualifiedName> state_type;
    };

    std::optional<CoroutineInfo> coroutine;
    std::vector<Param> params;

    std::unique_ptr<Stmt> body;

    bool is_static = false; // static void foo();
    bool is_virtual = false; // virtual void foo();
    bool is_const = false; // void foo() const;
    bool is_override = false; // void foo() override;
    bool is_dropped = false; // virtual void foo() = drop;
    bool is_pure = false; // virtual void foo() = 0;
    bool is_extern = false; // extern void foo();
};

// Data Structure

struct UnionDef : Node {
    Visibility visibility = Visibility::Private;
    std::string name;

    struct Field {
        std::unique_ptr<Type> type;
        std::string name;
    };

    std::vector<Field> fields;
};

struct Constructor : Node {
    std::vector<std::unique_ptr<Type>> param_types;
    std::vector<std::string> param_names;
    std::unique_ptr<Stmt> body;
    bool is_dropped = false;
};

struct Destructor : Node {
    bool is_virtual = false;
    bool is_override = false;
    bool is_dropped = false;
    std::unique_ptr<Stmt> body;
};

struct StructDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> members;
    std::unique_ptr<Constructor> ctor;
    std::unique_ptr<Destructor> dtor;
    std::unique_ptr<QualifiedName> base;
};

struct ClassDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;

    std::unique_ptr<Constructor> ctor;
    std::unique_ptr<Destructor> dtor;
    std::unique_ptr<QualifiedName> base;
    std::vector<std::unique_ptr<Node>> members;
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
    std::unique_ptr<QualifiedName> name;
    std::optional<std::string> alias;
};

struct NamespaceDecl : Node {
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> items;
};

struct FileProgram : Node {
    std::vector<std::unique_ptr<Node>> decls;
};
