#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <stdexcept>

#include "ast.hpp"

enum class SymbolKind {
    Variable,
    Function,
    Struct,
    Enum,
    Union,
    Namespace,
    Parameter,
    Field,
    Constructor,
    Destructor,
    ForwardStruct,
    ForwardFunction,
    EnumItem,
    Label
};

struct SemanticSymbol {
    std::string name;

    SymbolKind kind;

    Visibility visibility = Visibility::Private;

    Node* declaration = nullptr;

    Type* type = nullptr;

    bool is_static = false;
    bool is_extern = false;
    bool is_const = false;
    bool is_atomic = false;
    bool is_noreturn = false;
    bool is_coroutine = false;

    bool is_defined = false;
    bool is_forward = false;

    std::string qualified_name;

    SemanticSymbol() = default;

    SemanticSymbol(
        std::string name,
        SymbolKind kind,
        Node* declaration = nullptr
    )
        : name(std::move(name)),
          kind(kind),
          declaration(declaration)
    {}
};

static std::string qualifiedNameToString(
    const QualifiedName& name
) {
    std::string result;

    for (size_t i = 0; i < name.parts.size(); ++i) {

        if (i > 0)
            result += "::";

        result += name.parts[i];
    }

    return result;
}

static std::string qualifiedNameToString(
    const std::unique_ptr<QualifiedName>& name
) {
    if (!name)
        return {};

    return qualifiedNameToString(*name);
}

static std::string symbolKindToString(
    SymbolKind kind
) {
    switch (kind) {

        case SymbolKind::Variable:
            return "variable";

        case SymbolKind::Function:
            return "function";

        case SymbolKind::Struct:
            return "struct";

        case SymbolKind::Enum:
            return "enum";

        case SymbolKind::Union:
            return "union";

        case SymbolKind::Namespace:
            return "namespace";

        case SymbolKind::Parameter:
            return "parameter";

        case SymbolKind::Field:
            return "field";

        case SymbolKind::Constructor:
            return "constructor";

        case SymbolKind::Destructor:
            return "destructor";

        case SymbolKind::ForwardStruct:
            return "forward struct";

        case SymbolKind::ForwardFunction:
            return "forward function";

        case SymbolKind::EnumItem:
            return "enum item";

        case SymbolKind::Label:
            return "label";
    }

    return "unknown";
}

static bool isTypeSymbol(SymbolKind kind) {
    return
        kind == SymbolKind::Struct ||
        kind == SymbolKind::Enum ||
        kind == SymbolKind::Union ||
        kind == SymbolKind::ForwardStruct;
}

static bool isCallableSymbol(SymbolKind kind) {
    return
        kind == SymbolKind::Function ||
        kind == SymbolKind::ForwardFunction ||
        kind == SymbolKind::Constructor ||
        kind == SymbolKind::Destructor;
}

static bool isValueSymbol(SymbolKind kind) {
    return
        kind == SymbolKind::Variable ||
        kind == SymbolKind::Parameter ||
        kind == SymbolKind::Field ||
        kind == SymbolKind::EnumItem;
}

static bool isDeclarationSymbol(SymbolKind kind) {
    return
        kind == SymbolKind::Variable ||
        kind == SymbolKind::Function ||
        kind == SymbolKind::Struct ||
        kind == SymbolKind::Enum ||
        kind == SymbolKind::Union ||
        kind == SymbolKind::Namespace ||
        kind == SymbolKind::ForwardStruct ||
        kind == SymbolKind::ForwardFunction;
}

static std::unique_ptr<SemanticSymbol>
makeVariableSymbol(
    VarDecl::Item& item,
    VarDecl& decl
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = item.name;
    symbol->kind = SymbolKind::Variable;
    symbol->declaration = &decl;

    symbol->visibility = decl.visibility;

    symbol->type = decl.type.get();

    symbol->is_static = decl.is_static;
    symbol->is_extern = decl.is_extern;
    symbol->is_atomic = decl.is_atomic;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeFunctionSymbol(
    Function& fn
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(fn.name);

    symbol->kind = SymbolKind::Function;
    symbol->declaration = &fn;

    symbol->visibility = fn.visibility;

    symbol->type = fn.return_types.get();

    symbol->is_static = fn.is_static;
    symbol->is_const = fn.is_const;
    symbol->is_noreturn = fn.is_noreturn;
    symbol->is_coroutine = fn.is_coroutine;

    symbol->is_defined = fn.body != nullptr;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeForwardFunctionSymbol(
    ForwardDecl& decl
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(decl.name);

    symbol->kind =
        SymbolKind::ForwardFunction;

    symbol->declaration = &decl;

    symbol->visibility = decl.visibility;

    symbol->type = decl.return_types.get();

    symbol->is_static = decl.is_static;
    symbol->is_extern = decl.is_extern;
    symbol->is_noreturn = decl.is_noreturn;
    symbol->is_const = decl.is_const;
    symbol->is_coroutine = decl.is_coroutine;

    symbol->is_forward = true;
    symbol->is_defined = false;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeStructSymbol(
    StructDef& st
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(st.name);

    symbol->kind = SymbolKind::Struct;
    symbol->declaration = &st;

    symbol->visibility = st.visibility;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeEnumSymbol(
    EnumDef& en
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(en.name);

    symbol->kind = SymbolKind::Enum;
    symbol->declaration = &en;

    symbol->visibility = en.visibility;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeUnionSymbol(
    UnionDef& un
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(un.name);

    symbol->kind = SymbolKind::Union;
    symbol->declaration = &un;

    symbol->visibility = un.visibility;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeNamespaceSymbol(
    NamespaceDecl& ns
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(ns.name);

    symbol->kind = SymbolKind::Namespace;
    symbol->declaration = &ns;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeForwardStructSymbol(
    ForwardDecl& decl
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name =
        qualifiedNameToString(decl.name);

    symbol->kind =
        SymbolKind::ForwardStruct;

    symbol->declaration = &decl;

    symbol->visibility = decl.visibility;

    symbol->is_forward = true;
    symbol->is_defined = false;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeParameterSymbol(
    Function::Param& param,
    Function& fn
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = param.name;
    symbol->kind = SymbolKind::Parameter;

    symbol->declaration = &fn;

    symbol->type = param.type.get();

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeConstructorSymbol(
    Constructor& ctor
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = "construct";
    symbol->kind = SymbolKind::Constructor;

    symbol->declaration = &ctor;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeDestructorSymbol(
    Destructor& dtor
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = "drop";
    symbol->kind = SymbolKind::Destructor;

    symbol->declaration = &dtor;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeFieldSymbol(
    VarDecl::Item& item,
    VarDecl& decl
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = item.name;
    symbol->kind = SymbolKind::Field;

    symbol->declaration = &decl;

    symbol->visibility = decl.visibility;

    symbol->type = decl.type.get();

    symbol->is_atomic = decl.is_atomic;

    symbol->is_defined = true;

    return symbol;
}

static std::unique_ptr<SemanticSymbol>
makeEnumItemSymbol(
    EnumDef::Item& item,
    EnumDef& en
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = item.name;
    symbol->kind = SymbolKind::EnumItem;

    symbol->declaration = &en;

    symbol->visibility = en.visibility;

    symbol->is_const = true;
    symbol->is_defined = true;

    return symbol;
}

static bool sameSymbolName(
    const SemanticSymbol& a,
    const SemanticSymbol& b
) {
    return a.name == b.name;
}

static bool canRedeclare(
    const SemanticSymbol& oldSymbol,
    const SemanticSymbol& newSymbol
) {
    if (oldSymbol.kind == SymbolKind::ForwardFunction &&
        newSymbol.kind == SymbolKind::Function)
        return true;

    if (oldSymbol.kind == SymbolKind::ForwardStruct &&
        newSymbol.kind == SymbolKind::Struct)
        return true;

    return false;
}

static bool hasSameKind(
    const SemanticSymbol& a,
    const SemanticSymbol& b
) {
    return a.kind == b.kind;
}

static bool isPublic(
    const SemanticSymbol& symbol
) {
    return symbol.visibility == Visibility::Public;
}

static bool isPrivate(
    const SemanticSymbol& symbol
) {
    return symbol.visibility == Visibility::Private;
}
