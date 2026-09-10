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

/*
 * ---------------------------------------------------------
 * Symbol State
 * ---------------------------------------------------------
 *
 * Digunakan semantic analyzer untuk tracking lifetime/state
 * sebuah value.
 *
 * Valid  -> dapat digunakan
 * Moved  -> sudah dipindahkan dengan move
 */
enum class SymbolState {
    Valid,
    Moved
};

/*
 * ---------------------------------------------------------
 * Function Signature
 * ---------------------------------------------------------
 *
 * Metadata tambahan untuk semantic/type checking.
 *
 * SemanticSymbol::type tetap menunjuk ke return type seperti
 * implementasi lama. Signature ini hanya menambahkan informasi
 * parameter tanpa mengubah representasi lama.
 */
struct FunctionSignature {

    Type* return_type = nullptr;

    std::vector<Type*> parameters;

    bool is_coroutine = false;
    bool is_const = false;
    bool is_noreturn = false;
};


/*
 * ---------------------------------------------------------
 * Semantic Symbol
 * ---------------------------------------------------------
 */

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
    bool is_throws = false;

    bool is_defined = false;
    bool is_forward = false;

    std::string qualified_name;

    /*
     * State semantic value.
     *
     * Tidak menghapus symbol ketika move.
     *
     * Contoh:
     *
     *   Node a;
     *   Node b = move a;
     *
     * a tetap ada di SymbolTable,
     * tetapi state-nya menjadi Moved.
     */
    SymbolState state = SymbolState::Valid;

    /*
     * Optional function signature.
     *
     * Hanya digunakan untuk Function / ForwardFunction.
     */
    std::unique_ptr<FunctionSignature> function_signature;

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


/*
 * ---------------------------------------------------------
 * Qualified Name
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Symbol Kind
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Symbol Category Helpers
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Variable
 * ---------------------------------------------------------
 */

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

    /*
     * Initial state.
     *
     * Semantic analyzer nantinya dapat mengubah:
     *
     * Valid -> Moved
     */
    symbol->state = SymbolState::Valid;

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Function
 * ---------------------------------------------------------
 */

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

    /*
     * Qualified name.
     */
    symbol->qualified_name =
        qualifiedNameToString(fn.name);

    /*
     * Function signature.
     */
    symbol->function_signature =
        std::make_unique<FunctionSignature>();

    symbol->function_signature->return_type =
        fn.return_types.get();

    symbol->function_signature->is_coroutine =
        fn.is_coroutine;

    symbol->function_signature->is_const =
        fn.is_const;

    symbol->function_signature->is_noreturn =
        fn.is_noreturn;

    /*
     * Parameter types.
     */
    for (auto& param : fn.params) {

        if (param.type)
            symbol->function_signature->parameters.push_back(
                param.type.get()
            );
    }

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Forward Function
 * ---------------------------------------------------------
 */

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

    /*
     * Qualified name.
     */
    symbol->qualified_name =
        qualifiedNameToString(decl.name);

    /*
     * Function signature.
     */
    symbol->function_signature =
        std::make_unique<FunctionSignature>();

    symbol->function_signature->return_type =
        decl.return_types.get();

    symbol->function_signature->is_coroutine =
        decl.is_coroutine;

    symbol->function_signature->is_const =
        decl.is_const;

    symbol->function_signature->is_noreturn =
        decl.is_noreturn;

    /*
     * Jika ForwardDecl menyimpan parameter,
     * parameter types dapat dimasukkan di sini.
     *
     * Struktur AST lama tetap tidak diubah.
     */

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Struct
 * ---------------------------------------------------------
 */

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

    symbol->qualified_name =
        qualifiedNameToString(st.name);

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Enum
 * ---------------------------------------------------------
 */

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

    symbol->qualified_name =
        qualifiedNameToString(en.name);

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Union
 * ---------------------------------------------------------
 */

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

    symbol->qualified_name =
        qualifiedNameToString(un.name);

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Namespace
 * ---------------------------------------------------------
 */

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

    symbol->qualified_name =
        qualifiedNameToString(ns.name);

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Forward Struct
 * ---------------------------------------------------------
 */

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

    symbol->qualified_name =
        qualifiedNameToString(decl.name);

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Parameter
 * ---------------------------------------------------------
 */

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

    symbol->state = SymbolState::Valid;

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Constructor
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Destructor
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Field
 * ---------------------------------------------------------
 */

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

    symbol->state = SymbolState::Valid;

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Enum Item
 * ---------------------------------------------------------
 */

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

    /*
     * Enum item memiliki qualified name sendiri.
     *
     * Contoh:
     *
     *   Color::Red
     */
    symbol->qualified_name =
        qualifiedNameToString(en.name)
        + "::"
        + item.name;

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Label
 * ---------------------------------------------------------
 */

static std::unique_ptr<SemanticSymbol>
makeLabelSymbol(
    const std::string& name,
    Node* declaration = nullptr
) {
    auto symbol = std::make_unique<SemanticSymbol>();

    symbol->name = name;
    symbol->kind = SymbolKind::Label;

    symbol->declaration = declaration;

    symbol->is_defined = true;

    symbol->qualified_name = name;

    return symbol;
}


/*
 * ---------------------------------------------------------
 * Symbol Comparison
 * ---------------------------------------------------------
 */

static bool sameSymbolName(
    const SemanticSymbol& a,
    const SemanticSymbol& b
) {
    return a.name == b.name;
}


/*
 * ---------------------------------------------------------
 * Redeclaration
 * ---------------------------------------------------------
 */

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


/*
 * ---------------------------------------------------------
 * Kind Comparison
 * ---------------------------------------------------------
 */

static bool hasSameKind(
    const SemanticSymbol& a,
    const SemanticSymbol& b
) {
    return a.kind == b.kind;
}


/*
 * ---------------------------------------------------------
 * Move State Helpers
 * ---------------------------------------------------------
 *
 * Tambahan kecil supaya semantic analyzer tidak perlu
 * menyentuh field state secara langsung terus-menerus.
 */

static bool isMoved(
    const SemanticSymbol& symbol
) {
    return symbol.state == SymbolState::Moved;
}

static bool isValid(
    const SemanticSymbol& symbol
) {
    return symbol.state == SymbolState::Valid;
}

static void markMoved(
    SemanticSymbol& symbol
) {
    symbol.state = SymbolState::Moved;
}

static void markValid(
    SemanticSymbol& symbol
) {
    symbol.state = SymbolState::Valid;
}


/*
 * ---------------------------------------------------------
 * Visibility Helpers
 * ---------------------------------------------------------
 */

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

/*
 * ---------------------------------------------------------
 * Scope
 * ---------------------------------------------------------
 *
 * Satu lexical scope yang berisi kumpulan symbol.
 *
 * Contoh:
 *
 * global scope
 *     |
 *     +-- function scope
 *             |
 *             +-- block scope
 *
 * Lookup akan berjalan:
 *
 * current -> parent -> parent -> ... -> global
 *
 * Symbol tidak dihapus hanya karena keluar scope.
 * Scope yang sudah selesai nantinya dapat dibuang oleh
 * semantic analyzer.
 */
struct Scope {

    /*
     * Parent lexical scope.
     *
     * Tidak memiliki ownership terhadap parent.
     * SymbolTable yang mengatur lifetime Scope.
     */
    Scope* parent = nullptr;

    /*
     * Symbol yang dideklarasikan langsung pada scope ini.
     */
    std::unordered_map<
        std::string,
        std::unique_ptr<SemanticSymbol>
    > symbols;

    /*
     * Optional nama scope.
     *
     * Contoh:
     *
     * global
     * main
     * block
     * MyNamespace
     */
    std::string name;

    /*
     * Constructor.
     */
    Scope(
        Scope* parent = nullptr,
        std::string name = {}
    )
        : parent(parent),
          name(std::move(name))
    {}
};


/*
 * ---------------------------------------------------------
 * Symbol Table
 * ---------------------------------------------------------
 *
 * Mengatur lexical scopes dan symbol lookup.
 *
 * Scope hierarchy:
 *
 * Global
 *   |
 *   +-- Namespace
 *   |
 *   +-- Function
 *          |
 *          +-- Block
 *                 |
 *                 +-- Nested Block
 *
 * SymbolTable tidak mengubah SemanticSymbol.
 */
class SymbolTable {

private:

    /*
     * Global scope.
     *
     * Selalu ada selama SymbolTable hidup.
     */
    std::unique_ptr<Scope> global_scope;

    /*
     * Scope yang sedang aktif.
     *
     * Tidak memiliki ownership.
     */
    Scope* current_scope = nullptr;

    /*
     * Menyimpan scope aktif.
     *
     * Scope dibuat ketika enterScope().
     */
    std::vector<std::unique_ptr<Scope>> scope_storage;

public:

    /*
     * -----------------------------------------------------
     * Constructor
     * -----------------------------------------------------
     */

    SymbolTable() {

        global_scope =
            std::make_unique<Scope>(
                nullptr,
                "global"
            );

        current_scope =
            global_scope.get();
    }


    /*
     * -----------------------------------------------------
     * Current Scope
     * -----------------------------------------------------
     */

    Scope*
    current() const {

        return current_scope;
    }


    /*
     * -----------------------------------------------------
     * Global Scope
     * -----------------------------------------------------
     */

    Scope*
    global() const {

        return global_scope.get();
    }


    /*
     * -----------------------------------------------------
     * Enter Scope
     * -----------------------------------------------------
     *
     * Membuat lexical scope baru.
     *
     * Contoh:
     *
     * enterScope("function");
     * enterScope("block");
     *
     */

    Scope&
    enterScope(
        const std::string& name = {}
    ) {

        auto scope =
            std::make_unique<Scope>(
                current_scope,
                name
            );

        Scope* result =
            scope.get();

        scope_storage.push_back(
            std::move(scope)
        );

        current_scope = result;

        return *current_scope;
    }


    /*
     * -----------------------------------------------------
     * Exit Scope
     * -----------------------------------------------------
     *
     * Kembali ke parent scope.
     *
     * Global scope tidak dapat di-exit.
     */
    void
    exitScope() {

        if (!current_scope)
            return;

        if (current_scope == global_scope.get())
            return;

        current_scope =
            current_scope->parent;
    }


    /*
     * -----------------------------------------------------
     * Declare
     * -----------------------------------------------------
     *
     * Menambahkan symbol ke current scope.
     *
     * false -> redeclaration
     * true  -> berhasil
     *
     * Forward declaration dapat diredeclare oleh
     * declaration biasa melalui canRedeclare().
     */
    bool
    declare(
        std::unique_ptr<SemanticSymbol> symbol
    ) {

        if (!symbol)
            return false;

        if (!current_scope)
            return false;

        const std::string key =
            symbol->name;

        auto it =
            current_scope->symbols.find(key);

        /*
         * Belum ada symbol pada scope ini.
         */
        if (it == current_scope->symbols.end()) {

            current_scope->symbols.emplace(
                key,
                std::move(symbol)
            );

            return true;
        }

        /*
         * Sudah ada symbol.
         */
        SemanticSymbol& oldSymbol =
            *it->second;

        SemanticSymbol& newSymbol =
            *symbol;

        /*
         * Forward declaration -> definition.
         */
        if (canRedeclare(
                oldSymbol,
                newSymbol
            )) {

            /*
             * Pertahankan symbol baru sebagai
             * declaration definitif.
             */
            it->second =
                std::move(symbol);

            return true;
        }

        /*
         * Redeclaration biasa -> error.
         */
        return false;
    }


    /*
     * -----------------------------------------------------
     * Declare Raw
     * -----------------------------------------------------
     *
     * Convenience overload.
     */
    bool
    declare(
        SemanticSymbol symbol
    ) {

        return declare(
            std::make_unique<SemanticSymbol>(
                std::move(symbol)
            )
        );
    }


    /*
     * -----------------------------------------------------
     * Lookup Current Scope
     * -----------------------------------------------------
     *
     * Hanya mencari pada scope aktif.
     *
     * Tidak melihat parent.
     */
    SemanticSymbol*
    lookupCurrent(
        const std::string& name
    ) const {

        if (!current_scope)
            return nullptr;

        auto it =
            current_scope->symbols.find(name);

        if (it == current_scope->symbols.end())
            return nullptr;

        return it->second.get();
    }


    /*
     * -----------------------------------------------------
     * Lookup
     * -----------------------------------------------------
     *
     * Lexical lookup.
     *
     * current
     *   ↓
     * parent
     *   ↓
     * parent
     *   ↓
     * global
     */
    SemanticSymbol*
    lookup(
        const std::string& name
    ) const {

        Scope* scope =
            current_scope;

        while (scope) {

            auto it =
                scope->symbols.find(name);

            if (it != scope->symbols.end())
                return it->second.get();

            scope =
                scope->parent;
        }

        return nullptr;
    }


    /*
     * -----------------------------------------------------
     * Lookup From Scope
     * -----------------------------------------------------
     *
     * Berguna jika semantic analyzer ingin melakukan
     * lookup dari scope tertentu.
     */
    SemanticSymbol*
    lookup(
        Scope* scope,
        const std::string& name
    ) const {

        while (scope) {

            auto it =
                scope->symbols.find(name);

            if (it != scope->symbols.end())
                return it->second.get();

            scope =
                scope->parent;
        }

        return nullptr;
    }


    /*
     * -----------------------------------------------------
     * Contains
     * -----------------------------------------------------
     */

    bool
    contains(
        const std::string& name
    ) const {

        return lookup(name) != nullptr;
    }


    /*
     * -----------------------------------------------------
     * Contains Current
     * -----------------------------------------------------
     */

    bool
    containsCurrent(
        const std::string& name
    ) const {

        return lookupCurrent(name) != nullptr;
    }


    /*
     * -----------------------------------------------------
     * Remove Current
     * -----------------------------------------------------
     *
     * Menghapus symbol dari current scope.
     *
     * Jangan dipakai untuk move.
     *
     * Move seharusnya menggunakan state = Moved,
     * sehingga symbol masih dikenal oleh semantic analyzer.
     */
    bool
    removeCurrent(
        const std::string& name
    ) {

        if (!current_scope)
            return false;

        return
            current_scope->symbols.erase(name)
            != 0;
    }


    /*
     * -----------------------------------------------------
     * Scope Depth
     * -----------------------------------------------------
     */

    size_t
    depth() const {

        size_t result = 0;

        Scope* scope =
            current_scope;

        while (scope &&
               scope != global_scope.get()) {

            ++result;

            scope =
                scope->parent;
        }

        return result;
    }


    /*
     * -----------------------------------------------------
     * Is Global
     * -----------------------------------------------------
     */

    bool
    isGlobalScope() const {

        return
            current_scope ==
            global_scope.get();
    }
};
