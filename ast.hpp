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
    enum class Kind { Pointer, Reference, Array, FuncPtr } kind;

    std::vector<std::unique_ptr<Expr>> array_dims;

    std::vector<std::unique_ptr<Type>> func_params;
    std::unique_ptr<Type> func_return;
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

        Ternary,
        Range,

        Postfix,

        Call,
        Index,

        Cast,
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

    // Assign Expr
    struct {
        AssignOp op;
        std::unique_ptr<Expr> lhs;
        std::unique_ptr<Expr> rhs;
    } assign;

    // Ternary, You can use this for If Expr
    struct {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> then_expr;
        std::unique_ptr<Expr> else_expr;
    } ternary;

    // Pipe Expr
    struct {
        std::unique_ptr<Expr> left;
        std::unique_ptr<Expr> right;
    } pipe;

    // If Expr , Just a Plan and staying here temporarily! Not Official & cannot be used 
    struct {
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> then_expr;
        std::unique_ptr<Expr> else_expr;
    } if_expr;

    // Range Expr System Literal & Dynamic
    struct {
        std::unique_ptr<Expr> start;
        std::unique_ptr<Expr> end;
    } range;

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

    // cast
    struct {
        std::unique_ptr<Expr> base;
        std::vector<std::unique_ptr<Type>> chain;
    } cast;

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

struct Stmt : Node {
    enum class Kind {
        VarDecl, If, While, DoWhile, For,
        Return, Break, Continue, Match,
        ExprStmt, Block,
        Label, Jump,
        Delete
    } kind;

    struct {
        std::vector<std::unique_ptr<Stmt>> stmts;
    } block;

    struct {
        std::unique_ptr<Type> type;

        struct Item {
            std::unique_ptr<QualifiedName> name;
            std::unique_ptr<Expr> init;
            std::unique_ptr<Expr> ctor;
        };

        std::vector<Item> items;
        bool is_static = false;
        bool is_exported = false;
    } var_decl;

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
            Pattern pattern;
            std::unique_ptr<Stmt> body;
        };

        struct Pattern {
            enum class Kind {
                Default, Literal, Constructor, Binding } kind;
            std::unique_ptr<Expr> literal;
            std::unique_ptr<QualifiedName> ctor;
            std::vector<std::unique_ptr<Pattern>> args;
            std::string bind_name;
        };

        std::vector<Case> cases;
    } match_stmt;

    std::unique_ptr<Expr> expr_stmt;

    struct {
        std::unique_ptr<Expr> expr;
    } delete_stmt;

    std::string label;
    std::string jump_target;

    std::unique_ptr<Expr> ret;
};

// Top Level Scope Program

enum class Visibility { Private, Public, Protect };

struct Symbol {
    enum class SymbolOrigin {Source, Contract};
    enum class Kind {
        Function,
        Struct,
        Class,
        Enum,
        Union,
        GlobalVar
    } kind;
    Visibility visibility;
    bool exported = false;
    SymbolOrigin origin;
    std::string qualified_name;
    std::string signature;
    std::string mangled_name;
    std::vector<std::string> dependency_ids;
    const Node* decl = nullptr;
};

struct ForwardDecl : Node {
    Visibility visibility = Visibility::Private;
    enum class Kind { Struct, Class, Function } kind;
    std::unique_ptr<QualifiedName> name;
    std::unique_ptr<Type> return_type;
    std::vector<std::unique_ptr<Type>> params;
    bool is_static = false;
    bool is_virtual = false;
};

struct ExternDecl : Node {
    enum class Kind { Function, GlobalVar } kind;

    std::unique_ptr<Type> return_type;
    std::unique_ptr<QualifiedName> func_name;
    std::vector<std::unique_ptr<Type>> func_params;

    std::unique_ptr<Type> var_type;
    std::unique_ptr<QualifiedName> var_name;
    bool is_static = false;
};

struct Function : Node {
    Visibility visibility = Visibility::Private;

    std::unique_ptr<Type> return_type;
    std::unique_ptr<QualifiedName> name;

    struct Param {
        std::unique_ptr<Type> type;
        std::string name;
        std::unique_ptr<Expr> init;
    };

    std::vector<Param> params;

    std::unique_ptr<Stmt> body;

    bool is_static = false;
    bool is_virtual = false;
    bool is_const = false;
    bool is_override = false;
    bool is_deleted = false;
    bool is_pure = false;
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
    bool is_deleted = false;
};

struct Destructor : Node {
    bool is_virtual = false;
    bool is_override = false;
    bool is_deleted = false;
    std::unique_ptr<Stmt> body;
};

struct StructDef : Node {
    Visibility visibility = Visibility::Private;
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> members;
    std::unique_ptr<Constructor> ctor;
    std::unique_ptr<Destructor> dtor;
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
    bool is_union = false;
    std::unique_ptr<QualifiedName> name;

    struct Item {
        std::string name;
        std::unique_ptr<Expr> value; // const enum
        std::vector<std::unique_ptr<Type>> params; // union enum
    };

    std::vector<Item> items;
};

// Module & Program

struct UseDecl : Node {
    std::unique_ptr<QualifiedName> name;
    std::optional<std::string> alias;
};

struct PackageDecl : Node {
    std::unique_ptr<QualifiedName> name;
    std::vector<std::unique_ptr<Node>> items;
};

struct FileProgram : Node {
    std::vector<std::unique_ptr<Node>> decls;
};
