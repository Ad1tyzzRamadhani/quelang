#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <optional>
#include <utility>
#include <sstream>

#include "ast.hpp"

// Semantic diagnostics are intentionally kept separate from the AST.
enum class SemanticSeverity {
    Error,
    Warning
};

struct SemanticDiagnostic {
    SemanticSeverity severity = SemanticSeverity::Error;
    bool is_warning = false;
    std::string message;
    std::string file;
    int line = -1;
    int column = -1;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer() = default;

    // Returns true when the complete AST passes the implemented checks.
    bool analyze(FileProgram& program);

    const std::vector<SemanticDiagnostic>& diagnostics() const {
        return diagnostics_;
    }

    bool hasErrors() const;

    SymbolTable& symbols() { return symbols_; }
    const SymbolTable& symbols() const { return symbols_; }

private:
    struct TypeView;
    struct FunctionTypeView {
        std::vector<TypeView> parameters;
        TypeView* return_type = nullptr;
        bool is_coroutine = false;
    };
    struct TypeView {
        std::string base;
        std::vector<TypeModifier::Kind> modifiers;
        std::vector<TypeQualifier> qualifiers;
        FunctionTypeView& function;
        bool is_coroutine = false;
        bool valid = false;
        bool unknown = false;
    };

    struct FunctionContext {
        Function* function = nullptr;
        Type* return_type = nullptr;
        Type* throws_type = nullptr;
        bool is_coroutine = false;
        bool is_noreturn = false;
        bool is_throws = false;
        int loop_depth = 0;
        int switch_depth = 0;
        int catch_depth = 0;
        int unsafe_depth = 0;
        std::string struct_name;
    };

    SymbolTable symbols_;
    std::vector<SemanticDiagnostic> diagnostics_;
    std::vector<std::string> namespace_stack_;
    std::vector<FunctionContext> function_stack_;
    std::string current_struct_;

    // ---- diagnostics ----
    void error(const Node* node, const std::string& message);
    void warning(const Node* node, const std::string& message);

    // ---- top-level phases ----
    void registerProgram(FileProgram& program);
    void registerNode(Node& node);
    void registerNamespace(NamespaceDecl& ns);
    void registerStruct(StructDef& st);
    void registerEnum(EnumDef& en);
    void registerUnion(UnionDef& un);
    void registerFunction(Function& fn);
    void registerForward(ForwardDecl& decl);
    void registerVar(VarDecl& decl);

    void resolveProgram(FileProgram& program);
    void resolveNode(Node& node);
    void resolveNamespace(NamespaceDecl& ns);
    void resolveStruct(StructDef& st);
    void resolveEnum(EnumDef& en);
    void resolveUnion(UnionDef& un);
    void resolveFunction(Function& fn);
    void resolveForward(ForwardDecl& decl);
    void resolveVar(VarDecl& decl);

    // ---- scopes / declarations ----
    bool declareSymbol(std::unique_ptr<SemanticSymbol> symbol, const Node* node);
    SemanticSymbol* resolveSymbol(const std::string& name) const;
    SemanticSymbol* resolveSymbolInNamespace(const std::string& name) const;
    SemanticSymbol* resolveQualified(const QualifiedName& name) const;
    SemanticSymbol* resolveQualified(const std::unique_ptr<QualifiedName>& name) const;

    void analyzeFunctionBody(Function& fn);
    void analyzeConstructor(Constructor& ctor, const std::string& owner);
    void analyzeDestructor(Destructor& dtor, const std::string& owner);
    void analyzeBlock(Stmt& stmt);
    void analyzeStmt(Stmt& stmt);
    void analyzeVarDecl(VarDecl& decl);
    void analyzeSwitch(Stmt& stmt);
    void analyzeDo(Stmt& stmt);
    void analyzeFor(Stmt& stmt);

    // ---- types ----
    TypeView view(Type* type) const;
    TypeView view(const std::unique_ptr<Type>& type) const;
    std::string typeString(const TypeView& type) const;
    std::string typeString(Type* type) const;
    bool canConvert(const TypeView& from, const TypeView& to) const;
    bool sameType(const TypeView& a, const TypeView& b) const;
    bool sameType(Type* a, Type* b) const;
    bool isBuiltin(const std::string& name) const;
    bool isNumeric(const TypeView& type) const;
    bool isIntegral(const TypeView& type) const;
    bool isBoolean(const TypeView& type) const;
    bool isPointerLike(const TypeView& type) const;
    bool isVoid(const TypeView& type) const;
    bool isNone(const TypeView& type) const;
    bool isAssignable(const TypeView& lhs, const TypeView& rhs) const;
    bool validateType(Type* type, const Node* where, bool allow_void);
    bool validateTypeBase(const std::string& name, const Node* where, bool allow_void);

    // ---- expressions ----
    TypeView analyzeExpr(Expr* expr);
    TypeView analyzeLiteral(Literal* literal);
    TypeView analyzeUnary(Expr* expr);
    TypeView analyzeBinary(Expr* expr);
    TypeView analyzeAssign(Expr* expr);
    TypeView analyzeTernary(Expr* expr);
    TypeView analyzePostfix(Expr* expr);
    TypeView analyzeCast(Expr* expr);
    TypeView analyzeNew(Expr* expr);
    TypeView analyzeStructInit(Expr* expr);
    TypeView analyzeArrayLiteral(Expr* expr);

    bool requireBoolean(const TypeView& type, const Node* node, const char* context);
    bool requireNumeric(const TypeView& type, const Node* node, const char* context);
    bool requireIntegral(const TypeView& type, const Node* node, const char* context);

    // ---- members / calls ----
    struct MemberInfo {
        Type* type = nullptr;
        Function* function = nullptr;
        bool found = false;
        Visibility visibility = Visibility::Private;
        bool is_static = false;
        bool is_function = false;
    };

    MemberInfo findMember(const TypeView& base, const std::string& name) const;
    MemberInfo findStaticMember(const std::string& type_name, const std::string& name) const;
    MemberInfo findMemberInStruct(const StructDef& st, const std::string& name) const;
    MemberInfo findMemberInUnion(const UnionDef& un, const std::string& name) const;
    MemberInfo findMemberInEnum(const EnumDef& en, const std::string& name) const;
    bool checkCall(const TypeView& callee, const std::vector<std::unique_ptr<Expr>>& args,
                   const Node* node, TypeView& return_type);
    bool checkFunctionCall(const SemanticSymbol& symbol,
                           const std::vector<std::unique_ptr<Expr>>& args,
                           const Node* node, TypeView& return_type);

    // ---- statements / special rules ----
    bool isLValue(Expr* expr) const;
    SemanticSymbol* symbolForLValue(Expr* expr) const;
    bool checkReturn(Stmt& stmt);
    bool checkThrow(Stmt& stmt);
    bool checkResume(Stmt& stmt);
    bool checkYield(Stmt& stmt);
    bool checkDrop(Stmt& stmt);
    bool checkWipe(Stmt& stmt);
    bool checkLabel(Stmt& stmt);
    bool checkJump(Stmt& stmt);

    // ---- lookup helpers ----
    std::string qualifiedName(const QualifiedName& name) const;
    std::string qualifiedName(const std::unique_ptr<QualifiedName>& name) const;
    std::string currentNamespace() const;
    std::string currentFunctionName() const;
    bool accessAllowed(Visibility visibility, const std::string& owner) const;

    void reset();
};
