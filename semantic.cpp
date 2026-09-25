#include "semantic.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <functional>

namespace {

static std::string joinQualified(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += "::";
        out += parts[i];
    }
    return out;
}

} // namespace

bool SemanticAnalyzer::analyze(FileProgram& program) {
    reset();
    registerProgram(program);
    resolveProgram(program);
    return !hasErrors();
}

bool SemanticAnalyzer::hasErrors() const {
    return std::any_of(
        diagnostics_.begin(), diagnostics_.end(),
        [](const SemanticDiagnostic& d) {
            return d.severity == SemanticSeverity::Error;
        }
    );
}

void SemanticAnalyzer::reset() {
    diagnostics_.clear();
    namespace_stack_.clear();
    function_stack_.clear();
    current_struct_.clear();
    // SymbolTable intentionally has no reset API. A fresh analyzer is the
    // intended lifetime for one semantic-analysis run.
    symbols_ = SymbolTable();
}

void SemanticAnalyzer::error(const Node* node, const std::string& message) {
    SemanticDiagnostic d;
    d.severity = SemanticSeverity::Error;
    d.message = message;
    if (node) {
        d.file = node->file;
        d.line = node->line_start;
        d.column = node->column_start;
    }
    diagnostics_.push_back(std::move(d));
}

void SemanticAnalyzer::warning(const Node* node, const std::string& message) {
    SemanticDiagnostic d;
    d.severity = SemanticSeverity::Warning;
    d.message = message;
    if (node) {
        d.file = node->file;
        d.line = node->line_start;
        d.column = node->column_start;
    }
    diagnostics_.push_back(std::move(d));
}

std::string SemanticAnalyzer::qualifiedName(const QualifiedName& name) const {
    return joinQualified(name.parts);
}

std::string SemanticAnalyzer::qualifiedName(const std::unique_ptr<QualifiedName>& name) const {
    return name ? qualifiedName(*name) : std::string{};
}

std::string SemanticAnalyzer::currentNamespace() const {
    return joinQualified(namespace_stack_);
}

std::string SemanticAnalyzer::currentFunctionName() const {
    if (function_stack_.empty()) return {};
    if (!function_stack_.back().function || !function_stack_.back().function->name)
        return {};
    return qualifiedName(function_stack_.back().function->name);
}

bool SemanticAnalyzer::declareSymbol(std::unique_ptr<SemanticSymbol> symbol,
                                     const Node* node) {
    if (!symbol) return false;

    const std::string name = symbol->name;
    if (!symbols_.declare(std::move(symbol))) {
        error(node, "redeclaration of '" + name + "'");
        return false;
    }
    return true;
}

SemanticSymbol* SemanticAnalyzer::resolveQualified(const QualifiedName& name) const {
    const std::string full = qualifiedName(name);
    if (auto* s = symbols_.lookup(full)) return s;

    if (name.parts.size() == 1) {
        return resolveSymbol(name.parts.front());
    }

    return nullptr;
}

SemanticSymbol* SemanticAnalyzer::resolveQualified(const std::unique_ptr<QualifiedName>& name) const {
    return name ? resolveQualified(*name) : nullptr;
}

SemanticSymbol* SemanticAnalyzer::resolveSymbolInNamespace(const std::string& name) const {
    if (auto* direct = symbols_.lookup(name)) return direct;

    std::string ns = currentNamespace();
    while (!ns.empty()) {
        if (auto* s = symbols_.lookup(ns + "::" + name)) return s;
        const size_t p = ns.rfind("::");
        if (p == std::string::npos) break;
        ns.erase(p);
    }

    return nullptr;
}

SemanticSymbol* SemanticAnalyzer::resolveSymbol(const std::string& name) const {
    if (name.find("::") != std::string::npos) {
        if (auto* s = symbols_.lookup(name)) return s;
    }

    if (auto* s = symbols_.lookup(name)) return s;
    return resolveSymbolInNamespace(name);
}

void SemanticAnalyzer::registerProgram(FileProgram& program) {
    for (auto& decl : program.decls) {
        if (decl) registerNode(*decl);
    }
}

void SemanticAnalyzer::registerNode(Node& node) {
    if (auto* v = dynamic_cast<VarDecl*>(&node)) return registerVar(*v);
    if (auto* f = dynamic_cast<Function*>(&node)) return registerFunction(*f);
    if (auto* s = dynamic_cast<StructDef*>(&node)) return registerStruct(*s);
    if (auto* e = dynamic_cast<EnumDef*>(&node)) return registerEnum(*e);
    if (auto* u = dynamic_cast<UnionDef*>(&node)) return registerUnion(*u);
    if (auto* n = dynamic_cast<NamespaceDecl*>(&node)) return registerNamespace(*n);
    if (auto* fd = dynamic_cast<ForwardDecl*>(&node)) return registerForward(*fd);
    if (dynamic_cast<UseDecl*>(&node)) return;
}

void SemanticAnalyzer::registerVar(VarDecl& decl) {
    for (auto& item : decl.items) {
        auto symbol = std::make_unique<SemanticSymbol>();
        symbol->name = currentNamespace().empty()
            ? item.name
            : currentNamespace() + "::" + item.name;
        symbol->kind = SymbolKind::Variable;
        symbol->declaration = &decl;
        symbol->visibility = decl.visibility;
        symbol->type = decl.type.get();
        symbol->is_static = decl.is_static;
        symbol->is_extern = decl.is_extern;
        symbol->is_atomic = decl.is_atomic;
        symbol->is_defined = true;
        declareSymbol(std::move(symbol), &decl);
    }
}

void SemanticAnalyzer::registerFunction(Function& fn) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = currentNamespace().empty() ? qualifiedName(fn.name) : currentNamespace() + "::" + qualifiedName(fn.name);
    symbol->kind = SymbolKind::Function;
    symbol->declaration = &fn;
    symbol->visibility = fn.visibility;
    symbol->type = fn.return_types.get();
    symbol->is_static = fn.is_static;
    symbol->is_const = fn.is_const;
    symbol->is_noreturn = fn.is_noreturn;
    symbol->is_coroutine = fn.is_coroutine;
    symbol->is_throws = fn.is_throws;
    symbol->is_defined = fn.body != nullptr;
    symbol->qualified_name = symbol->name;
    symbol->function_signature = std::make_unique<FunctionSignature>();
    symbol->function_signature->return_type = fn.return_types.get();
    symbol->function_signature->throws_type = fn.throws_type.get();
    symbol->function_signature->is_coroutine = fn.is_coroutine;
    symbol->function_signature->is_const = fn.is_const;
    symbol->function_signature->is_noreturn = fn.is_noreturn;
    symbol->function_signature->is_throws = fn.is_throws;
    for (auto& p : fn.params) {
        if (p.type) symbol->function_signature->parameters.push_back(p.type.get());
    }
    declareSymbol(std::move(symbol), &fn);
}

void SemanticAnalyzer::registerForward(ForwardDecl& decl) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = currentNamespace().empty() ? qualifiedName(decl.name) : currentNamespace() + "::" + qualifiedName(decl.name);
    symbol->kind = decl.kind == ForwardDecl::Kind::Struct
        ? SymbolKind::ForwardStruct
        : SymbolKind::ForwardFunction;
    symbol->declaration = &decl;
    symbol->visibility = decl.visibility;
    symbol->type = decl.return_types.get();
    symbol->is_static = decl.is_static;
    symbol->is_extern = decl.is_extern;
    symbol->is_const = decl.is_const;
    symbol->is_noreturn = decl.is_noreturn;
    symbol->is_coroutine = decl.is_coroutine;
    symbol->is_throws = decl.is_throws;
    symbol->is_forward = true;
    symbol->is_defined = false;
    symbol->qualified_name = symbol->name;
    if (decl.kind == ForwardDecl::Kind::Function) {
        symbol->function_signature = std::make_unique<FunctionSignature>();
        symbol->function_signature->return_type = decl.return_types.get();
        symbol->function_signature->throws_type = decl.throws_type.get();
        symbol->function_signature->is_coroutine = decl.is_coroutine;
        symbol->function_signature->is_const = decl.is_const;
        symbol->function_signature->is_noreturn = decl.is_noreturn;
        symbol->function_signature->is_throws = decl.is_throws;
        for (auto& p : decl.params) {
            if (p) symbol->function_signature->parameters.push_back(p.get());
        }
    }
    declareSymbol(std::move(symbol), &decl);
}

void SemanticAnalyzer::registerStruct(StructDef& st) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = currentNamespace().empty() ? qualifiedName(st.name) : currentNamespace() + "::" + qualifiedName(st.name);
    symbol->kind = SymbolKind::Struct;
    symbol->declaration = &st;
    symbol->visibility = st.visibility;
    symbol->is_defined = true;
    symbol->default_field = st.default_field.value_or("");
    symbol->qualified_name = symbol->name;
    declareSymbol(std::move(symbol), &st);
}

void SemanticAnalyzer::registerEnum(EnumDef& en) {
    const std::string enum_name = currentNamespace().empty()
        ? qualifiedName(en.name)
        : currentNamespace() + "::" + qualifiedName(en.name);

    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = enum_name;
    symbol->kind = SymbolKind::Enum;
    symbol->declaration = &en;
    symbol->visibility = en.visibility;
    symbol->is_defined = true;
    symbol->qualified_name = enum_name;
    declareSymbol(std::move(symbol), &en);

    // Enum items are real semantic symbols so qualified names such as
    // EnumName::Item can be resolved without inventing a second lookup path.
    for (auto& item : en.items) {
        auto item_symbol = makeEnumItemSymbol(item, en);
        item_symbol->name = enum_name + "::" + item.name;
        item_symbol->qualified_name = item_symbol->name;
        declareSymbol(std::move(item_symbol), &en);
    }
}

void SemanticAnalyzer::registerUnion(UnionDef& un) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = currentNamespace().empty() ? qualifiedName(un.name) : currentNamespace() + "::" + qualifiedName(un.name);
    symbol->kind = SymbolKind::Union;
    symbol->declaration = &un;
    symbol->visibility = un.visibility;
    symbol->is_defined = true;
    symbol->default_field = un.default_field.value_or("");
    symbol->qualified_name = symbol->name;
    declareSymbol(std::move(symbol), &un);
}

void SemanticAnalyzer::registerNamespace(NamespaceDecl& ns) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = qualifiedName(ns.name);
    symbol->kind = SymbolKind::Namespace;
    symbol->declaration = &ns;
    symbol->is_defined = true;
    symbol->qualified_name = symbol->name;
    declareSymbol(std::move(symbol), &ns);

    const std::string old = currentNamespace();
    namespace_stack_.push_back(qualifiedName(ns.name));
    // Declarations inside a namespace keep their AST qualified names.
    for (auto& item : ns.items) {
        if (item) registerNode(*item);
    }
    namespace_stack_.pop_back();
    (void)old;
}

void SemanticAnalyzer::resolveProgram(FileProgram& program) {
    for (auto& decl : program.decls) {
        if (decl) resolveNode(*decl);
    }
}

void SemanticAnalyzer::resolveNode(Node& node) {
    if (auto* v = dynamic_cast<VarDecl*>(&node)) return resolveVar(*v);
    if (auto* f = dynamic_cast<Function*>(&node)) return resolveFunction(*f);
    if (auto* s = dynamic_cast<StructDef*>(&node)) return resolveStruct(*s);
    if (auto* e = dynamic_cast<EnumDef*>(&node)) return resolveEnum(*e);
    if (auto* u = dynamic_cast<UnionDef*>(&node)) return resolveUnion(*u);
    if (auto* n = dynamic_cast<NamespaceDecl*>(&node)) return resolveNamespace(*n);
    if (auto* fd = dynamic_cast<ForwardDecl*>(&node)) return resolveForward(*fd);
    if (dynamic_cast<UseDecl*>(&node)) return;
}

void SemanticAnalyzer::resolveNamespace(NamespaceDecl& ns) {
    namespace_stack_.push_back(qualifiedName(ns.name));
    for (auto& item : ns.items) {
        if (item) resolveNode(*item);
    }
    namespace_stack_.pop_back();
}

void SemanticAnalyzer::resolveVar(VarDecl& decl) {
    if (!validateType(decl.type.get(), &decl, false)) return;

    for (auto& item : decl.items) {
        for (auto& dim : item.array_dims) {
            if (dim) {
                TypeView t = analyzeLiteral(dim.get());
                requireIntegral(t, dim.get(), "array dimension");
            }
        }
        if (item.init) {
            TypeView target = view(decl.type.get());

            if (item.init->kind == Expr::Kind::ArrayLiteral) {
                if (item.array_dims.empty()) {
                    error(item.init.get(), "array literal requires an array declaration type");
                } else {
                    // Type represents the element type; array_dims represents the
                    // array shape in the current AST. Keep that representation
                    // instead of inventing an array TypeModifier.
                    TypeView init = analyzeArrayLiteral(item.init.get());
                    if (init.valid && !sameType(target, init)) {
                        error(item.init.get(), "array literal element type " +
                            typeString(init) + " does not match array element type " +
                            typeString(target));
                    }
                }
            } else if (item.init->kind == Expr::Kind::StructInit) {
                SemanticSymbol* type_symbol = resolveSymbol(target.base);
                auto* st = type_symbol ? dynamic_cast<StructDef*>(type_symbol->declaration) : nullptr;
                if (st) {
                    std::unordered_set<std::string> initialized;
                    for (auto& field : item.init->struct_init.fields) {
                        if (!field.second) continue;
                        if (!field.first.has_value()) {
                            error(field.second.get(), "struct initializer field is missing a name");
                            continue;
                        }
                        const std::string& name = *field.first;
                        if (!initialized.insert(name).second) {
                            error(field.second.get(), "duplicate struct initializer field '" + name + "'");
                            continue;
                        }
                        MemberInfo member = findMemberInStruct(*st, name);
                        if (!member.found || !member.type || member.is_function) {
                            error(field.second.get(), "unknown struct initializer field '" + name + "'");
                            continue;
                        }
                        TypeView actual = analyzeExpr(field.second.get());
                        TypeView expected = view(member.type);
                        if (!isAssignable(expected, actual)) {
                            error(field.second.get(), "cannot initialize struct field '" + name + "' of type " +
                                typeString(expected) + " with " + typeString(actual));
                        }
                    }
                } else {
                    error(item.init.get(), "struct initializer requires a struct type, got " + typeString(target));
                }
            } else {
                TypeView init = analyzeExpr(item.init.get());
                if (!isAssignable(target, init)) {
                    error(item.init.get(), "cannot initialize '" + item.name + "' of type " +
                        typeString(target) + " with " + typeString(init));
                }
            }
        }
    }
}

void SemanticAnalyzer::resolveForward(ForwardDecl& decl) {
    if (decl.kind == ForwardDecl::Kind::Struct) return;
    validateType(decl.return_types.get(), &decl, true);
    if (decl.throws_type) validateType(decl.throws_type.get(), &decl, false);
    for (auto& p : decl.params) {
        if (p) validateType(p.get(), &decl, false);
    }
}

void SemanticAnalyzer::resolveEnum(EnumDef& en) {
    if (en.underlying_type) validateType(en.underlying_type.get(), &en, false);
    std::unordered_set<std::string> names;
    for (auto& item : en.items) {
        if (!names.insert(item.name).second)
            error(&en, "duplicate enum item '" + item.name + "'");
        if (item.value) analyzeExpr(item.value.get());
    }
}

void SemanticAnalyzer::resolveUnion(UnionDef& un) {
    std::unordered_set<std::string> names;
    for (auto& field : un.fields) {
        if (!names.insert(field.name).second)
            error(&un, "duplicate union field '" + field.name + "'");
        validateType(field.type.get(), &un, false);
    }
    if (un.default_field && names.find(*un.default_field) == names.end())
        error(&un, "unknown default union field '" + *un.default_field + "'");
}

void SemanticAnalyzer::resolveStruct(StructDef& st) {
    const std::string old_struct = current_struct_;
    current_struct_ = qualifiedName(st.name);

    std::unordered_set<std::string> field_names;
    for (auto& member : st.members) {
        if (auto* field = dynamic_cast<VarDecl*>(member.get())) {
            for (auto& item : field->items) {
                if (!field_names.insert(item.name).second)
                    error(field, "duplicate struct field '" + item.name + "'");
            }
            validateType(field->type.get(), field, false);
            for (auto& item : field->items) {
                if (item.init) analyzeExpr(item.init.get());
            }
        } else if (member) {
            resolveNode(*member);
        }
    }

    if (st.default_field) {
        if (field_names.find(*st.default_field) == field_names.end())
            error(&st, "unknown default struct field '" + *st.default_field + "'");
    }

    if (st.ctor) analyzeConstructor(*st.ctor, current_struct_);
    if (st.default_ctor) analyzeConstructor(*st.default_ctor, current_struct_);
    if (st.dtor) analyzeDestructor(*st.dtor, current_struct_);

    current_struct_ = old_struct;
}

void SemanticAnalyzer::resolveFunction(Function& fn) {
    validateType(fn.return_types.get(), &fn, true);
    if (fn.throws_type) validateType(fn.throws_type.get(), &fn, false);
    for (auto& p : fn.params) {
        validateType(p.type.get(), &fn, false);
        if (p.init) analyzeExpr(p.init.get());
    }
    if (fn.body) analyzeFunctionBody(fn);
}

void SemanticAnalyzer::analyzeFunctionBody(Function& fn) {
    FunctionContext ctx;
    ctx.function = &fn;
    ctx.return_type = fn.return_types.get();
    ctx.throws_type = fn.throws_type.get();
    ctx.is_coroutine = fn.is_coroutine;
    ctx.is_noreturn = fn.is_noreturn;
    ctx.is_throws = fn.is_throws;
    ctx.struct_name = current_struct_;
    function_stack_.push_back(std::move(ctx));

    symbols_.enterScope(qualifiedName(fn.name));

    for (auto& param : fn.params) {
        auto symbol = std::make_unique<SemanticSymbol>();
        symbol->name = param.name;
        symbol->kind = SymbolKind::Parameter;
        symbol->declaration = &fn;
        symbol->type = param.type.get();
        symbol->is_defined = true;
        symbol->state = SymbolState::Valid;
        declareSymbol(std::move(symbol), &fn);
    }

    if (fn.body) {
        analyzeStmt(*fn.body);

        // A small control-flow pass is kept here instead of changing the AST.
        // It answers only one question needed by semantic validation: can the
        // function body reach its end normally?
        std::function<bool(const Expr*)> exprFallsThrough = [&](const Expr* expr) -> bool {
            if (!expr) return true;
            if (expr->kind != Expr::Kind::Postfix) return true;
            if (expr->postfix.ops.empty() || expr->postfix.ops.back().kind != Expr::PostfixOp::Kind::Call)
                return true;

            const Expr* base = expr->postfix.base.get();
            if (base && base->kind == Expr::Kind::Ident && base->ident) {
                SemanticSymbol* s = resolveSymbol(qualifiedName(*base->ident));
                return !(s && s->is_noreturn);
            }
            return true;
        };

        std::function<bool(const Stmt*)> canFallThrough = [&](const Stmt* stmt) -> bool {
            if (!stmt) return true;
            switch (stmt->kind) {
                case Stmt::Kind::Return:
                case Stmt::Kind::ThrowStmt:
                    return false;
                case Stmt::Kind::ExprStmt:
                    return exprFallsThrough(stmt->expr_stmt.get());
                case Stmt::Kind::Block: {
                    for (const auto& child : stmt->block.stmts) {
                        if (child && !canFallThrough(child.get())) return false;
                    }
                    return true;
                }
                case Stmt::Kind::If: {
                    if (!stmt->if_stmt.else_block) return true;
                    if (canFallThrough(stmt->if_stmt.then_block.get())) return true;
                    for (const auto& branch : stmt->if_stmt.elifs) {
                        if (canFallThrough(branch.block.get())) return true;
                    }
                    return canFallThrough(stmt->if_stmt.else_block.get());
                }
                case Stmt::Kind::Unsafe:
                    return canFallThrough(stmt->unsafe_stmt.stmt.get());
                case Stmt::Kind::SwitchCase: {
                    bool has_default = false;
                    for (const auto& c : stmt->switch_stmt.cases) {
                        if (c.is_default) has_default = true;
                        if (!c.body || canFallThrough(c.body.get())) return true;
                    }
                    return !has_default ? true : false;
                }
                case Stmt::Kind::DoStmt:
                    return canFallThrough(stmt->do_stmt.body.get());
                default:
                    // Loops, break/continue, declarations and other statements
                    // can conservatively fall through.
                    return true;
            }
        };

        const bool falls_through = canFallThrough(fn.body.get());
        if (fn.is_noreturn) {
            if (falls_through)
                error(&fn, "noreturn function can reach the end normally");
        } else if (!view(fn.return_types.get()).valid || !isVoid(view(fn.return_types.get()))) {
            if (falls_through)
                error(&fn, "non-void function can reach the end without returning a value");
        }
    }

    symbols_.exitScope();
    function_stack_.pop_back();
}

void SemanticAnalyzer::analyzeConstructor(Constructor& ctor, const std::string& owner) {
    FunctionContext ctx;
    ctx.return_type = nullptr;
    ctx.is_coroutine = false;
    ctx.struct_name = owner;
    function_stack_.push_back(std::move(ctx));
    symbols_.enterScope("construct");
    for (auto& param : ctor.params) {
        if (param.type) validateType(param.type.get(), &ctor, false);
        auto symbol = std::make_unique<SemanticSymbol>();
        symbol->name = param.name;
        symbol->kind = SymbolKind::Parameter;
        symbol->declaration = &ctor;
        symbol->type = param.type.get();
        symbol->is_defined = true;
        declareSymbol(std::move(symbol), &ctor);
        if (param.init) analyzeExpr(param.init.get());
    }
    if (ctor.body) analyzeStmt(*ctor.body);
    symbols_.exitScope();
    function_stack_.pop_back();
}

void SemanticAnalyzer::analyzeDestructor(Destructor& dtor, const std::string& owner) {
    FunctionContext ctx;
    ctx.return_type = nullptr;
    ctx.is_coroutine = false;
    ctx.struct_name = owner;
    function_stack_.push_back(std::move(ctx));
    symbols_.enterScope("drop");
    if (dtor.body) analyzeStmt(*dtor.body);
    symbols_.exitScope();
    function_stack_.pop_back();
}

void SemanticAnalyzer::analyzeBlock(Stmt& stmt) {
    symbols_.enterScope("block");
    for (auto& child : stmt.block.stmts) {
        if (child) analyzeStmt(*child);
    }
    symbols_.exitScope();
}

void SemanticAnalyzer::analyzeVarDecl(VarDecl& decl) {
    if (!validateType(decl.type.get(), &decl, false)) return;
    for (auto& item : decl.items) {
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
        symbol->state = SymbolState::Valid;
        std::cout << "DECLARE dat: " << qualifiedName(*symbol->type->base) << " mods="
        << symbol->type->modifiers.size()
        << "\n";
        if (!declareSymbol(std::move(symbol), &decl)) continue;

        if (item.init) {
            TypeView target = view(decl.type.get());
            if (item.init->kind == Expr::Kind::ArrayLiteral) {
                if (item.array_dims.empty()) {
                    error(item.init.get(), "array literal requires an array declaration type");
                } else {
                    TypeView init = analyzeArrayLiteral(item.init.get());
                    if (init.valid && !sameType(target, init)) {
                        error(item.init.get(), "array literal element type " +
                            typeString(init) + " does not match array element type " +
                            typeString(target));
                    }
                }
            } else if (item.init->kind == Expr::Kind::StructInit) {
                SemanticSymbol* type_symbol = resolveSymbol(target.base);
                auto* st = type_symbol ? dynamic_cast<StructDef*>(type_symbol->declaration) : nullptr;
                if (st) {
                    std::unordered_set<std::string> initialized;
                    for (auto& field : item.init->struct_init.fields) {
                        if (!field.second) continue;
                        if (!field.first.has_value()) {
                            error(field.second.get(), "struct initializer field is missing a name");
                            continue;
                        }
                        const std::string& name = *field.first;
                        if (!initialized.insert(name).second) {
                            error(field.second.get(), "duplicate struct initializer field '" + name + "'");
                            continue;
                        }
                        MemberInfo member = findMemberInStruct(*st, name);
                        if (!member.found || !member.type || member.is_function) {
                            error(field.second.get(), "unknown struct initializer field '" + name + "'");
                            continue;
                        }
                        TypeView actual = analyzeExpr(field.second.get());
                        TypeView expected = view(member.type);
                        if (!isAssignable(expected, actual)) {
                            error(field.second.get(), "cannot initialize struct field '" + name + "' of type " +
                                typeString(expected) + " with " + typeString(actual));
                        }
                    }
                } else {
                    error(item.init.get(), "struct initializer requires a struct type, got " + typeString(target));
                }
            } else {
                TypeView init = analyzeExpr(item.init.get());
                if (!isAssignable(target, init)) {
                    error(item.init.get(), "cannot initialize '" + item.name + "' of type " +
                        typeString(target) + " with " + typeString(init));
                }
            }
        }
    }
}

void SemanticAnalyzer::analyzeSwitch(Stmt& stmt) {
    TypeView value = analyzeExpr(stmt.switch_stmt.expr.get());
    if (!value.valid) return;
    function_stack_.back().switch_depth++;
    std::unordered_set<std::string> seen;
    for (auto& c : stmt.switch_stmt.cases) {
        for (auto& lit : c.values) {
            if (!lit) continue;
            TypeView t = analyzeLiteral(lit.get());
            if (!isAssignable(value, t))
                error(lit.get(), "switch case type " + typeString(t) +
                    " does not match switch expression type " + typeString(value));
            const std::string key = lit->value;
            if (!seen.insert(key).second)
                error(lit.get(), "duplicate switch case '" + key + "'");
        }
        if (c.body) analyzeStmt(*c.body);
    }
    function_stack_.back().switch_depth--;
}

void SemanticAnalyzer::analyzeDo(Stmt& stmt) {
    analyzeStmt(*stmt.do_stmt.body);
    if (stmt.do_stmt.cond) {
        TypeView cond = analyzeExpr(stmt.do_stmt.cond.get());
        requireBoolean(cond, stmt.do_stmt.cond.get(), "do/while condition");
    }
    if (stmt.do_stmt.iscatch) {
        if (!function_stack_.back().is_throws && !function_stack_.back().throws_type) {
            warning(&stmt, "do/catch is used in a function without a throws contract");
        }
        symbols_.enterScope("catch");
        function_stack_.back().catch_depth++;
        if (!stmt.do_stmt.error_var.items.empty()) {
            VarDecl& err = stmt.do_stmt.error_var;
            validateType(err.type.get(), &err, false);
            for (auto& item : err.items) {
                auto symbol = std::make_unique<SemanticSymbol>();
                symbol->name = item.name;
                symbol->kind = SymbolKind::Variable;
                symbol->declaration = &err;
                symbol->type = err.type.get();
                symbol->is_defined = true;
                declareSymbol(std::move(symbol), &err);
            }
        }
        if (stmt.do_stmt.catch_body) analyzeStmt(*stmt.do_stmt.catch_body);
        function_stack_.back().catch_depth--;
        symbols_.exitScope();
    }
}

void SemanticAnalyzer::analyzeFor(Stmt& stmt) {
    symbols_.enterScope("for");
    if (!stmt.for_stmt.init.items.empty()) {
        VarDecl& init = stmt.for_stmt.init;
        validateType(init.type.get(), &init, false);
        for (auto& item : init.items) {
            auto symbol = std::make_unique<SemanticSymbol>();
            symbol->name = item.name;
            symbol->kind = SymbolKind::Variable;
            symbol->declaration = &init;
            symbol->type = init.type.get();
            symbol->is_defined = true;
            declareSymbol(std::move(symbol), &init);
            if (item.init) analyzeExpr(item.init.get());
        }
    }
    if (stmt.for_stmt.source) analyzeExpr(stmt.for_stmt.source.get());
    function_stack_.back().loop_depth++;
    if (stmt.for_stmt.body) analyzeStmt(*stmt.for_stmt.body);
    function_stack_.back().loop_depth--;
    symbols_.exitScope();
}

void SemanticAnalyzer::analyzeStmt(Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::VarDecl:
            analyzeVarDecl(stmt.var_decl);
            return;
        case Stmt::Kind::If: {
            TypeView c = analyzeExpr(stmt.if_stmt.cond.get());
            requireBoolean(c, stmt.if_stmt.cond.get(), "if condition");
            if (stmt.if_stmt.then_block) analyzeStmt(*stmt.if_stmt.then_block);
            for (auto& e : stmt.if_stmt.elifs) {
                TypeView ec = analyzeExpr(e.cond.get());
                requireBoolean(ec, e.cond.get(), "elif condition");
                if (e.block) analyzeStmt(*e.block);
            }
            if (stmt.if_stmt.else_block) analyzeStmt(*stmt.if_stmt.else_block);
            return;
        }
        case Stmt::Kind::While: {
            TypeView c = analyzeExpr(stmt.while_stmt.cond.get());
            requireBoolean(c, stmt.while_stmt.cond.get(), "while condition");
            function_stack_.back().loop_depth++;
            if (stmt.while_stmt.body) analyzeStmt(*stmt.while_stmt.body);
            function_stack_.back().loop_depth--;
            return;
        }
        case Stmt::Kind::DoStmt:
            analyzeDo(stmt);
            return;
        case Stmt::Kind::For:
            analyzeFor(stmt);
            return;
        case Stmt::Kind::Resume:
            checkResume(stmt);
            return;
        case Stmt::Kind::Return:
            checkReturn(stmt);
            return;
        case Stmt::Kind::Break:
            if (function_stack_.back().loop_depth == 0 && function_stack_.back().switch_depth == 0)
                error(&stmt, "'break' is not inside a loop or switch");
            return;
        case Stmt::Kind::Continue:
            if (function_stack_.back().loop_depth == 0)
                error(&stmt, "'continue' is not inside a loop");
            return;
        case Stmt::Kind::SwitchCase:
            analyzeSwitch(stmt);
            return;
        case Stmt::Kind::ExprStmt:
            if (stmt.expr_stmt) analyzeExpr(stmt.expr_stmt.get());
            return;
        case Stmt::Kind::Block:
            analyzeBlock(stmt);
            return;
        case Stmt::Kind::UseStmt:
            if (stmt.use_stmt && stmt.use_stmt->target && !resolveQualified(stmt.use_stmt->target))
                error(stmt.use_stmt.get(), "unknown use target '" + qualifiedName(stmt.use_stmt->target) + "'");
            return;
        case Stmt::Kind::ThrowStmt:
            checkThrow(stmt);
            return;
        case Stmt::Kind::Label:
            checkLabel(stmt);
            return;
        case Stmt::Kind::Jump:
            checkJump(stmt);
            return;
        case Stmt::Kind::Unsafe:
            function_stack_.back().unsafe_depth++;
            if (stmt.unsafe_stmt.stmt) analyzeStmt(*stmt.unsafe_stmt.stmt);
            function_stack_.back().unsafe_depth--;
            return;
        case Stmt::Kind::Drop:
            checkDrop(stmt);
            return;
        case Stmt::Kind::Wipe:
            checkWipe(stmt);
            return;
        case Stmt::Kind::Yield:
            checkYield(stmt);
            return;
    }
}

SemanticAnalyzer::TypeView SemanticAnalyzer::view(Type* type) const {
    TypeView out;
    if (!type || !type->base) return out;
    out.base = qualifiedName(*type->base);
    out.valid = true;
    out.qualifiers = type->qualifiers;
    for (auto& mod : type->modifiers) {
        out.modifiers.push_back(mod.kind);
        if (mod.kind == TypeModifier::Kind::FuncPtr) {
            FunctionTypeView fn;
            out.is_coroutine = mod.is_coroutine;
            for (auto& param : mod.func_params)
            fn.parameters.push_back(view(param.get()));
            if (mod.func_return)
            fn.return_type = view(mod.func_return.get());
            out.function = std::move(fn);
        }
    }
    return out;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::view(const std::unique_ptr<Type>& type) const {
    return view(type.get());
}

std::string SemanticAnalyzer::typeString(const TypeView& type) const {
    if (!type.valid) return "<invalid>";
    std::string out;
    for (auto q : type.qualifiers) {
        if (q == TypeQualifier::Const) out += "const ";
        else if (q == TypeQualifier::Volatile) out += "volatile ";
        else if (q == TypeQualifier::Atomic) out += "atomic ";
        else if (q == TypeQualifier::Unaligned) out += "unaligned ";
    }
    out += type.base;
    for (auto m : type.modifiers) {
        if (m == TypeModifier::Kind::Pointer) out += "*";
        else if (m == TypeModifier::Kind::Reference) out += "&";
        else if (m == TypeModifier::Kind::Restrict) out += " restrict";
        else if (m == TypeModifier::Kind::FuncPtr) out += "(fnptr)";
    }
    return out;
}

std::string SemanticAnalyzer::typeString(Type* type) const {
    return typeString(view(type));
}

bool SemanticAnalyzer::canConvert(const TypeView& from, const TypeView& to) const {
    if (!from.valid || !to.valid)
        return false;

    if (sameType(from, to))
        return true;

    // T& -> T
    if (from.modifiers.size() == 1 &&
        from.modifiers[0] == TypeModifier::Kind::Reference &&
        to.modifiers.empty()) {

        TypeView value = from;
        value.modifiers.clear();

        return sameType(value, to);
    }

    return false;
}

bool SemanticAnalyzer::sameType(const TypeView& a, const TypeView& b) const {
    if (!a.valid || !b.valid) return false;
    if ((a.base != b.base || a.modifiers != b.modifiers)) return false;
    if (a.function.has_value() != b.function.has_value())
    return false;

    if (a.function && b.function) {
        const auto& af = *a.function;
        const auto& bf = *b.function;
 
        if (af.is_coroutine != bf.is_coroutine)
            return false;

        if (af.parameters.size() != bf.parameters.size())
            return false;

        for (size_t i = 0; i < af.parameters.size(); ++i) {
            if (!sameType(af.parameters[i], bf.parameters[i]))
            return false;
        }

        if (af.return_type == nullptr && bf.return_type == nullptr)
            return false;

        if (af.return_type &&
            !sameType(*af.return_type, *bf.return_type))
            return false;
    }
    return true;
}

bool SemanticAnalyzer::sameType(Type* a, Type* b) const {
    return sameType(view(a), view(b));
}

bool SemanticAnalyzer::isBuiltin(const std::string& name) const {
    static const std::unordered_set<std::string> builtins = {
        "i8","i16","i32","i64","u8","u16","u32","u64",
        "f32","f64","char8","char16","char32","bool","void",
        "usize","isize"
    };
    return builtins.find(name) != builtins.end();
}

bool SemanticAnalyzer::isNumeric(const TypeView& type) const {
    if (!type.valid || !type.modifiers.empty()) return false;
    return type.base == "i8" || type.base == "i16" || type.base == "i32" || type.base == "i64" ||
           type.base == "u8" || type.base == "u16" || type.base == "u32" || type.base == "u64" ||
           type.base == "f32" || type.base == "f64" || type.base == "usize" || type.base == "isize";
}

bool SemanticAnalyzer::isIntegral(const TypeView& type) const {
    if (!type.valid || !type.modifiers.empty()) return false;
    return type.base == "i8" || type.base == "i16" || type.base == "i32" || type.base == "i64" ||
           type.base == "u8" || type.base == "u16" || type.base == "u32" || type.base == "u64" ||
           type.base == "usize" || type.base == "isize" ||
           type.base == "char8" || type.base == "char16" || type.base == "char32";
}

bool SemanticAnalyzer::isBoolean(const TypeView& type) const {
    return type.valid && type.modifiers.empty() && type.base == "bool";
}

bool SemanticAnalyzer::isPointerLike(const TypeView& type) const {
    if (!type.valid || type.modifiers.empty()) return false;
    const auto m = type.modifiers.back();
    return m == TypeModifier::Kind::Pointer ||
           m == TypeModifier::Kind::Reference ||
           m == TypeModifier::Kind::FuncPtr;
}

bool SemanticAnalyzer::isVoid(const TypeView& type) const {
    return type.valid && type.modifiers.empty() && type.base == "void";
}

bool SemanticAnalyzer::isNone(const TypeView& type) const {
    return type.valid && type.base == "none";
}

bool SemanticAnalyzer::isAssignable(const TypeView& lhs, const TypeView& rhs) const {
    if (!lhs.valid || !rhs.valid) return false;
    if (canConvert(lhs, rhs)) return true;
    if (sameType(lhs, rhs)) return true;
    if ((isNone(rhs) || (rhs.valid && rhs.base == "null")) && isPointerLike(lhs)) return true;
    return false;
}

bool SemanticAnalyzer::validateTypeBase(const std::string& name, const Node* where, bool allow_void) {
    if (name.empty()) {
        error(where, "missing type name");
        return false;
    }
    if (name == "void") {
        if (!allow_void) {
            error(where, "void is not allowed here");
            return false;
        }
        return true;
    }
    if (isBuiltin(name)) return true;
    SemanticSymbol* symbol = resolveSymbol(name);
    if (!symbol) {
        error(where, "unknown type '" + name + "'");
        return false;
    }
    if (symbol->kind != SymbolKind::Struct && symbol->kind != SymbolKind::Enum &&
        symbol->kind != SymbolKind::Union && symbol->kind != SymbolKind::ForwardStruct) {
        error(where, "'" + name + "' is not a type");
        return false;
    }
    return true;
}

bool SemanticAnalyzer::validateType(Type* type, const Node* where, bool allow_void) {
    if (!type) {
        error(where, "missing type");
        return false;
    }
    if (!type->base) {
        error(where, "missing type base");
        return false;
    }
    bool ok = validateTypeBase(qualifiedName(*type->base), where, allow_void);
    for (auto& mod : type->modifiers) {
        if (mod.kind == TypeModifier::Kind::FuncPtr) {
            if (!mod.func_return) {
                error(where, "function pointer is missing return type");
                ok = false;
            } else {
                ok = validateType(mod.func_return.get(), where, true) && ok;
            }
            for (auto& p : mod.func_params) {
                if (!p) { ok = false; continue; }
                ok = validateType(p.get(), where, false) && ok;
            }
            if (mod.throws_type)
                ok = validateType(mod.throws_type.get(), where, false) && ok;
        }
    }
    return ok;
}

bool SemanticAnalyzer::requireBoolean(const TypeView& type, const Node* node, const char* context) {
    if (isBoolean(type)) return true;
    error(node, std::string(context) + " requires bool, got " + typeString(type));
    return false;
}

bool SemanticAnalyzer::requireNumeric(const TypeView& type, const Node* node, const char* context) {
    if (isNumeric(type)) return true;
    error(node, std::string(context) + " requires a numeric type, got " + typeString(type));
    return false;
}

bool SemanticAnalyzer::requireIntegral(const TypeView& type, const Node* node, const char* context) {
    if (isIntegral(type)) return true;
    error(node, std::string(context) + " requires an integral type, got " + typeString(type));
    return false;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeLiteral(Literal* literal) {
    TypeView t;
    if (!literal) return t;
    t.valid = true;
    switch (literal->kind) {
        case Literal::Kind::Number: t.base = "i32"; break;
        case Literal::Kind::Float: t.base = "f64"; break;
        case Literal::Kind::Hex: t.base = "u64"; break;
        case Literal::Kind::Binary: t.base = "u64"; break;
        case Literal::Kind::Char: t.base = "char32"; break;
        case Literal::Kind::String: t.base = "char8"; t.modifiers.push_back(TypeModifier::Kind::Pointer); break;
        case Literal::Kind::RawString: t.base = "char8"; t.modifiers.push_back(TypeModifier::Kind::Pointer); break;
        case Literal::Kind::True:
        case Literal::Kind::False: t.base = "bool"; break;
        case Literal::Kind::Null: t.base = "null"; break;
        case Literal::Kind::None: t.base = "none"; break;
    }
    return t;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeExpr(Expr* expr) {
    TypeView invalid;
    if (!expr) return invalid;
    switch (expr->kind) {
        case Expr::Kind::Literal: return analyzeLiteral(expr->literal.get());
        case Expr::Kind::Ident: {
            if (!expr->ident) return invalid;
            const std::string name = qualifiedName(*expr->ident);
            if (name == "this") {
                if (current_struct_.empty()) {
                    error(expr, "'this' is only valid inside a struct member context");
                    return invalid;
                }
                TypeView t;
                t.base = current_struct_;
                t.valid = true;
                t.modifiers.push_back(TypeModifier::Kind::Reference);
                return t;
            }
            SemanticSymbol* symbol = resolveSymbol(name);
            if (!symbol) {
                error(expr, "unknown identifier '" + name + "'");
                return invalid;
            }
            if (symbol->kind == SymbolKind::Struct || symbol->kind == SymbolKind::Enum ||
                symbol->kind == SymbolKind::Union || symbol->kind == SymbolKind::Namespace) {
                TypeView t;
                t.base = symbol->qualified_name.empty() ? symbol->name : symbol->qualified_name;
                t.valid = true;
                return t;
            }
            if (symbol->kind == SymbolKind::EnumItem) {
                auto* en = dynamic_cast<EnumDef*>(symbol->declaration);
                if (!en) {
                    error(expr, "enum item '" + name + "' has no enum declaration");
                    return invalid;
                }
                TypeView t;
                t.base = qualifiedName(en->name);
                if (!currentNamespace().empty() && name.rfind(currentNamespace() + "::", 0) == 0)
                    t.base = currentNamespace() + "::" + t.base;
                t.valid = true;
                return t;
            }
            if (symbol->state == SymbolState::Moved) {
                error(expr, "use of moved value '" + name + "'");
                return invalid;
            }
            if (!symbol->type) {
                if (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::ForwardFunction)
                    return {};
                error(expr, "symbol '" + name + "' has no type");
                return invalid;
            }
            return view(symbol->type);
        }
        case Expr::Kind::Unary: return analyzeUnary(expr);
        case Expr::Kind::Binary: return analyzeBinary(expr);
        case Expr::Kind::Assign: return analyzeAssign(expr);
        case Expr::Kind::Ternary: return analyzeTernary(expr);
        case Expr::Kind::Postfix: return analyzePostfix(expr);
        case Expr::Kind::Cast: return analyzeCast(expr);
        case Expr::Kind::New: return analyzeNew(expr);
        case Expr::Kind::Move:
            return analyzeUnary(expr);
        case Expr::Kind::StructInit: return analyzeStructInit(expr);
        case Expr::Kind::ArrayLiteral: return analyzeArrayLiteral(expr);
    }
    return invalid;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeUnary(Expr* expr) {
    TypeView t = analyzeExpr(expr->unary.expr.get());
    if (!t.valid) return t;
    switch (expr->unary.op) {
        case UnaryOp::Neg:
            requireNumeric(t, expr, "unary '-'");
            return t;
        case UnaryOp::Not:
            requireBoolean(t, expr, "unary 'not'");
            return t;
        case UnaryOp::BitNot:
            requireIntegral(t, expr, "bitwise '~'");
            return t;
        case UnaryOp::Deref: {
            if (t.modifiers.empty()) {
                error(expr, "cannot dereference non-pointer type " + typeString(t));
                return {};
            }
            const auto last = t.modifiers.back();
            if (last != TypeModifier::Kind::Pointer && last != TypeModifier::Kind::Reference) {
                error(expr, "cannot dereference this type");
                return {};
            }
            t.modifiers.pop_back();
            return t;
        }
        case UnaryOp::Ref:
            if (!isLValue(expr->unary.expr.get()))
                error(expr, "reference operator requires an lvalue");
            t.modifiers.push_back(TypeModifier::Kind::Pointer);
            return t;
        case UnaryOp::Move: {
            SemanticSymbol* symbol = symbolForLValue(expr->unary.expr.get());
            if (!symbol) {
                error(expr, "move requires a named value");
                return t;
            }
            if (symbol->state == SymbolState::Moved) {
                error(expr, "cannot move already-moved value '" + symbol->name + "'");
                return t;
            }
            symbol->state = SymbolState::Moved;
            return t;
        }
    }
    return t;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeBinary(Expr* expr) {
    TypeView lhs = analyzeExpr(expr->binary.lhs.get());
    TypeView rhs = analyzeExpr(expr->binary.rhs.get());
    if (!lhs.valid || !rhs.valid) return {};

    switch (expr->binary.op) {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            requireNumeric(lhs, expr, "arithmetic operator");
            requireNumeric(rhs, expr, "arithmetic operator");
            if (!sameType(lhs, rhs)) {
                error(expr, "arithmetic operands must have matching types: " +
                    typeString(lhs) + " and " + typeString(rhs));
                return {};
            }
            return lhs;
        case BinaryOp::Eq:
        case BinaryOp::Ne:
            if (!isAssignable(lhs, rhs) && !isAssignable(rhs, lhs))
                error(expr, "comparison operands are incompatible: " + typeString(lhs) +
                    " and " + typeString(rhs));
            return TypeView{ "bool", {}, {}, true, false };
        case BinaryOp::Lt:
        case BinaryOp::Lte:
        case BinaryOp::Gt:
        case BinaryOp::Gte:
            requireNumeric(lhs, expr, "relational operator");
            requireNumeric(rhs, expr, "relational operator");
            if (!sameType(lhs, rhs))
                error(expr, "relational operands must have matching types");
            return TypeView{ "bool", {}, {}, true, false };
        case BinaryOp::And:
        case BinaryOp::Or:
        case BinaryOp::Xor:
            requireBoolean(lhs, expr, "logical operator");
            requireBoolean(rhs, expr, "logical operator");
            return TypeView{ "bool", {}, {}, true, false };
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::Shl:
        case BinaryOp::Shr:
            requireIntegral(lhs, expr, "bitwise operator");
            requireIntegral(rhs, expr, "bitwise operator");
            if (!sameType(lhs, rhs))
                error(expr, "bitwise operands must have matching types");
            return lhs;
        case BinaryOp::Coalesce:
            if (!isPointerLike(lhs)) {
                error(expr, "left operand of coalesce must be pointer-like");
                return {};
            }
            if (!isAssignable(lhs, rhs) && !isNone(rhs))
                error(expr, "coalesce operands are incompatible");
            return lhs;
        case BinaryOp::In:
            if (!rhs.valid) return {};
            return TypeView{ "bool", {}, {}, true, false };
    }
    return {};
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeAssign(Expr* expr) {
    if (!isLValue(expr->assign.lhs.get())) {
        error(expr, "left side of assignment must be an lvalue");
        return {};
    }
    TypeView lhs = analyzeExpr(expr->assign.lhs.get());
    TypeView rhs = analyzeExpr(expr->assign.rhs.get());
    if (std::find(lhs.qualifiers.begin(), lhs.qualifiers.end(), TypeQualifier::Const) != lhs.qualifiers.end()) {
        error(expr, "cannot assign to const value");
        return {};
    }
    if (!isAssignable(lhs, rhs)) {
        error(expr, "cannot assign " + typeString(rhs) + " to " + typeString(lhs));
        return {};
    }
    if (auto* symbol = symbolForLValue(expr->assign.lhs.get()))
        symbol->state = SymbolState::Valid;

    if (expr->assign.op != AssignOp::Assign) {
        if (!isNumeric(lhs) || !isNumeric(rhs))
            error(expr, "compound assignment requires numeric operands");
    }
    return lhs;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeTernary(Expr* expr) {
    TypeView c = analyzeExpr(expr->ternary.cond.get());
    requireBoolean(c, expr->ternary.cond.get(), "ternary condition");
    TypeView a = analyzeExpr(expr->ternary.then_expr.get());
    TypeView b = analyzeExpr(expr->ternary.else_expr.get());
    if (!sameType(a, b)) {
        error(expr, "ternary branches must have matching types");
        return {};
    }
    return a;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeCast(Expr* expr) {
    analyzeExpr(expr->cast.base.get());
    if (!validateType(expr->cast.target.get(), expr, true)) return {};
    return view(expr->cast.target.get());
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeNew(Expr* expr) {
    if (!expr->new_expr.type) {
        if (expr->new_expr.alloc_size) {
            TypeView size = analyzeLiteral(expr->new_expr.alloc_size.get());
            requireIntegral(size, expr, "raw new allocation size");
            TypeView out;
            out.base = "u8";
            out.modifiers.push_back(TypeModifier::Kind::Pointer);
            out.valid = true;
            return out;
        }
        error(expr, "new expression has no type");
        return {};
    }
    SemanticSymbol* symbol = resolveQualified(expr->new_expr.type);
    const std::string name = qualifiedName(expr->new_expr.type);
    if (!isBuiltin(name) && !symbol) {
        error(expr, "unknown type in new expression '" + name + "'");
        return {};
    }
    for (auto& arg : expr->new_expr.args) analyzeExpr(arg.get());
    for (auto& dim : expr->new_expr.array_dims) {
        TypeView t = analyzeExpr(dim.get());
        requireIntegral(t, dim.get(), "array dimension");
    }
    TypeView out;
    out.base = name;
    out.valid = true;
    out.modifiers.push_back(TypeModifier::Kind::Pointer);
    return out;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeStructInit(Expr* expr) {
    TypeView out;
    for (auto& field : expr->struct_init.fields) {
        if (field.second) analyzeExpr(field.second.get());
    }
    // The current AST does not store the target struct type for StructInit.
    // Therefore field-name/type matching cannot be performed without inventing
    // information that is not present in ast.hpp.
    return out;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzeArrayLiteral(Expr* expr) {
    TypeView element_type;
    for (auto& item : expr->array_items) {
        TypeView t = analyzeExpr(item.get());
        if (!t.valid) continue;
        if (!element_type.valid) element_type = t;
        else if (!sameType(element_type, t))
            error(item.get(), "array literal elements must have matching types");
    }
    // The current AST represents array shape separately through VarDecl::array_dims.
    // Therefore the expression can expose its element type, while the contextual
    // declaration check validates that element type against the declared array type.
    return element_type;
}

SemanticAnalyzer::MemberInfo SemanticAnalyzer::findMemberInStruct(
    const StructDef& st, const std::string& name) const {
    MemberInfo result;
    for (auto& member : st.members) {
        if (auto* var = dynamic_cast<VarDecl*>(member.get())) {
            for (auto& item : var->items) {
                if (item.name == name) {
                    result.found = true;
                    result.type = var->type.get();
                    result.visibility = var->visibility;
                    result.is_static = var->is_static;
                    result.is_function = false;
                    return result;
                }
            }
        }
        if (auto* fn = dynamic_cast<Function*>(member.get())) {
            /*std::cout << "METHOD: "
              << qualifiedName(fn->name)
              << " parts="
              << fn->name->parts.size()
              << "\n";*/
            if (qualifiedName(fn->name) == name || (fn->name && fn->name->parts.size() == 1 && fn->name->parts[0] == name)) {
                result.found = true;
                result.function = fn;
                result.visibility = fn->visibility;
                result.is_static = fn->is_static;
                result.is_function = true;
                return result;
            }
        }
        if (auto* nested = dynamic_cast<StructDef*>(member.get())) {
            if (qualifiedName(nested->name) == name) {
                result.found = true;
                result.visibility = nested->visibility;
                return result;
            }
        }
    }
    return result;
}

SemanticAnalyzer::MemberInfo SemanticAnalyzer::findMemberInUnion(
    const UnionDef& un, const std::string& name) const {
    MemberInfo result;
    for (auto& field : un.fields) {
        if (field.name == name) {
            result.found = true;
            result.type = field.type.get();
            result.visibility = un.visibility;
            return result;
        }
    }
    return result;
}

SemanticAnalyzer::MemberInfo SemanticAnalyzer::findMemberInEnum(
    const EnumDef& en, const std::string& name) const {
    MemberInfo result;
    for (auto& item : en.items) {
        if (item.name == name) {
            result.found = true;
            result.visibility = en.visibility;
            return result;
        }
    }
    return result;
}

SemanticAnalyzer::MemberInfo SemanticAnalyzer::findStaticMember(
    const std::string& type_name, const std::string& name) const {
    MemberInfo result;
    SemanticSymbol* s = resolveSymbol(type_name);
    if (!s || !s->declaration) return result;
    if (auto* st = dynamic_cast<StructDef*>(s->declaration))
        return findMemberInStruct(*st, name);
    if (auto* un = dynamic_cast<UnionDef*>(s->declaration))
        return findMemberInUnion(*un, name);
    if (auto* en = dynamic_cast<EnumDef*>(s->declaration))
        return findMemberInEnum(*en, name);
    return result;
}

SemanticAnalyzer::MemberInfo SemanticAnalyzer::findMember(
    const TypeView& base, const std::string& name) const {
    MemberInfo result;
    std::string type_name = base.base;
    SemanticSymbol* s = resolveSymbol(type_name);
    if (!s || !s->declaration) return result;
    if (auto* st = dynamic_cast<StructDef*>(s->declaration))
        return findMemberInStruct(*st, name);
    if (auto* un = dynamic_cast<UnionDef*>(s->declaration))
        return findMemberInUnion(*un, name);
    return result;
}

bool SemanticAnalyzer::accessAllowed(Visibility visibility, const std::string& owner) const {
    if (visibility == Visibility::Public) return true;
    return !owner.empty() && owner == current_struct_;
}

bool SemanticAnalyzer::checkFunctionCall(
    const SemanticSymbol& symbol,
    const std::vector<std::unique_ptr<Expr>>& args,
    const Node* node,
    TypeView& return_type) {
    if (!symbol.function_signature) {
        error(node, "symbol is not callable");
        return false;
    }
    const auto& sig = *symbol.function_signature;
    if (sig.parameters.size() != args.size()) {
        error(node, "function '" + symbol.name + "' expects " +
            std::to_string(sig.parameters.size()) + " argument(s), got " +
            std::to_string(args.size()));
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < args.size(); ++i) {
        TypeView actual = analyzeExpr(args[i].get());
        TypeView expected = view(sig.parameters[i]);
        if (!isAssignable(expected, actual)) {
            error(args[i].get(), "argument " + std::to_string(i + 1) +
                " of '" + symbol.name + "' expects " + typeString(expected) +
                ", got " + typeString(actual));
            ok = false;
        }
    }
    return_type = view(sig.return_type);
    return ok;
}

bool SemanticAnalyzer::checkCall(
    const TypeView& callee,
    const std::vector<std::unique_ptr<Expr>>& args,
    const Node* node,
    TypeView& return_type) {
    (void)callee;
    (void)args;
    error(node, "expression is not callable");
    return_type = {};
    return false;
}

SemanticAnalyzer::TypeView SemanticAnalyzer::analyzePostfix(Expr* expr) {
    TypeView current;
    SemanticSymbol* pending_symbol = nullptr;
    Function* pending_function = nullptr;
    std::string pending_owner;

    if (expr->postfix.base &&
        expr->postfix.base->kind == Expr::Kind::Ident &&
        expr->postfix.base->ident) {

        const std::string base_name =
        qualifiedName(*expr->postfix.base->ident);

        if (base_name == "this") {
            current = analyzeExpr(expr->postfix.base.get());
        } else {
            pending_symbol = resolveSymbol(base_name);

            if (pending_symbol &&
                pending_symbol->state == SymbolState::Moved)
                error(expr, "use of moved value '" + base_name + "'");

            if (pending_symbol &&
                pending_symbol->kind != SymbolKind::Function &&
                pending_symbol->kind != SymbolKind::ForwardFunction) {

                current = view(pending_symbol->type);

                if (pending_symbol->kind == SymbolKind::Struct ||
                    pending_symbol->kind == SymbolKind::Enum ||
                    pending_symbol->kind == SymbolKind::Union) {

                    current.base =
                        pending_symbol->qualified_name.empty()
                            ? pending_symbol->name
                            : pending_symbol->qualified_name;

                    current.valid = true;
                 }
            }
        }
    } else {
        current = analyzeExpr(expr->postfix.base.get());
    }

    for (auto& op : expr->postfix.ops) {
        /*std::cout << "POSTFIX BASE: "
          << (expr->postfix.base && expr->postfix.base->ident
              ? qualifiedName(*expr->postfix.base->ident)
              : "<non-ident>")
          << "\n";

std::cout << "CURRENT VALID: "
          << current.valid
          << "\n";

std::cout << "CURRENT BASE: "
          << current.base
          << "\n";

std::cout << "CURRENT MODIFIERS: "
          << current.modifiers.size()
          << "\n";*/
        switch (op.kind) {
            case Expr::PostfixOp::Kind::MemberAccess: {
                if (!current.valid) {
                    error(expr, "member access requires a value with a known type");
                    return {};
                }
                MemberInfo m = findMember(current, op.name);
                if (!m.found) {
                    error(expr, "type '" + typeString(current) + "' has no member '" + op.name + "'");
                    return {};
                }
                if (!accessAllowed(m.visibility, current.base)) {
                    error(expr, "member '" + op.name + "' is private");
                    return {};
                }
                pending_symbol = nullptr;
                pending_function = m.function;
                pending_owner = current.base;
                current = m.is_function ? TypeView{} : view(m.type);
                break;
            }
            case Expr::PostfixOp::Kind::Arrow: {
                if (current.modifiers.empty() ||
                    (current.modifiers.back() != TypeModifier::Kind::Pointer &&
                    current.modifiers.back() != TypeModifier::Kind::Reference)) {

                    error(expr, "'" + op.name + "' requires a pointer/reference base");
                    return {};
                }

                current.modifiers.pop_back();

                MemberInfo m = findMember(current, op.name);

                if (!m.found) {
                    error(expr,"type '" + typeString(current) +"' has no member '" + op.name + "'");
                    return {};
                }

    if (!accessAllowed(m.visibility, current.base)) {
        error(
            expr,
            "member '" + op.name + "' is private"
        );
        return {};
    }

    pending_symbol = nullptr;
    pending_function = m.function;
    pending_owner = current.base;

    current = m.is_function
        ? TypeView{}
        : view(m.type);

    break;
            }
            case Expr::PostfixOp::Kind::SafeArrow: {
                if (current.modifiers.empty() ||
                    (current.modifiers.back() != TypeModifier::Kind::Pointer &&
                     current.modifiers.back() != TypeModifier::Kind::Reference)) {
                    error(expr, "'" + op.name + "' requires a pointer/reference base");
                    return {};
                }
                current.modifiers.pop_back();
                MemberInfo m = findMember(current, op.name);
                if (!m.found) {
                    error(expr, "type '" + typeString(current) + "' has no member '" + op.name + "'");
                    return {};
                }
                if (!accessAllowed(m.visibility, current.base)) {
                    error(expr, "member '" + op.name + "' is private");
                    return {};
                }
                pending_symbol = nullptr;
                pending_function = m.function;
                pending_owner = current.base;
                current = m.is_function ? TypeView{} : view(m.type);
                break;
            }
            case Expr::PostfixOp::Kind::StaticAccess:
            case Expr::PostfixOp::Kind::Scope: {
                std::string type_name = current.base;
                MemberInfo m = findStaticMember(type_name, op.name);
                if (!m.found) {
                    error(expr, "type '" + type_name + "' has no static member '" + op.name + "'");
                    return {};
                }
                pending_symbol = nullptr;
                pending_function = m.function;
                pending_owner = type_name;
                if (m.is_function) current = {};
                else if (m.type) current = view(m.type);
                else {
                    current = {};
                    if (resolveSymbol(type_name) &&
                        resolveSymbol(type_name)->kind == SymbolKind::Enum) {
                        current.base = type_name;
                        current.valid = true;
                    }
                }
                break;
            }
            case Expr::PostfixOp::Kind::Call: {
                TypeView ret;
                /*std::cout
    << "CALL BASE KIND: "
    << static_cast<int>(expr->postfix.base->kind)
    << "\n";
                std::cout
    << "PENDING FUNCTION: "
    << (pending_function ? "YES" : "NO")
    << " owner=" << pending_owner
    << "\n";
                std::cout
    << "CALL: pending_function="
    << (pending_function ? "YES" : "NO")
    << " pending_symbol="
    << (pending_symbol ? "YES" : "NO")
    << "\n";*/
                if (pending_function && pending_function->is_coroutine) {
                    for (auto& p : pending_function->params)
                    if (p.type) {
                        TypeModifier mod;
                        modifiers.func_params.push_back(p.type);
                    }
                    mod.is_coroutine = true;
                    mod.throws_type = pending_function->throws_type;
                    pending_function->return_types->modifiers = std::move(mod);
                }
                if (pending_function) {
                    SemanticSymbol callable;
                    callable.name = pending_owner + "::" +
                        (pending_function->name && !pending_function->name->parts.empty()
                            ? pending_function->name->parts.back() : "function");
                    callable.kind = SymbolKind::Function;
                    callable.function_signature = std::make_unique<FunctionSignature>();
                    callable.function_signature->return_type = pending_function->return_types.get();
                    callable.function_signature->throws_type = pending_function->throws_type.get();
                    callable.function_signature->is_coroutine = pending_function->is_coroutine;
                    callable.function_signature->is_const = pending_function->is_const;
                    callable.function_signature->is_noreturn = pending_function->is_noreturn;
                    callable.function_signature->is_throws = pending_function->is_throws;
    
                    for (auto& p : pending_function->params)
                        if (p.type) callable.function_signature->parameters.push_back(p.type.get());
                    checkFunctionCall(callable, op.args, expr, ret);
                } else if (pending_symbol &&
                           (pending_symbol->kind == SymbolKind::Function ||
                            pending_symbol->kind == SymbolKind::ForwardFunction)) {
                    checkFunctionCall(*pending_symbol, op.args, expr, ret);
                } else if (pending_symbol && pending_symbol->type) {
                    // Function-pointer variables/parameters carry their full
                    // signature in TypeModifier::FuncPtr. The existing TypeView
                    // intentionally keeps only modifier kinds, so inspect the
                    // original Type here rather than changing the public AST.
                    const Type* fn_type = pending_symbol->type;
                    const TypeModifier* fn_mod = nullptr;
                    for (auto it = fn_type->modifiers.rbegin(); it != fn_type->modifiers.rend(); ++it) {
                        if (it->kind == TypeModifier::Kind::FuncPtr) {
                            fn_mod = &*it;
                            break;
                        }
                    }
                    if (!fn_mod) {
                        error(expr, "expression is not callable");
                        return {};
                    }
                    if (fn_mod->func_params.size() != op.args.size()) {
                        error(expr, "function pointer expects " +
                            std::to_string(fn_mod->func_params.size()) + " argument(s), got " +
                            std::to_string(op.args.size()));
                        return {};
                    }
                    bool ok = true;
                    for (size_t i = 0; i < op.args.size(); ++i) {
                        TypeView actual = analyzeExpr(op.args[i].get());
                        TypeView expected = view(fn_mod->func_params[i].get());
                        if (!isAssignable(expected, actual)) {
                            error(op.args[i].get(), "argument " + std::to_string(i + 1) +
                                " of function pointer expects " + typeString(expected) +
                                ", got " + typeString(actual));
                            ok = false;
                        }
                    }
                    if (fn_mod->func_return) ret = view(fn_mod->func_return.get());
                    else {
                        error(expr, "function pointer is missing return type");
                        ok = false;
                    }
                    if (!ok) return {};
                } else {
                    error(expr, "expression is not callable");
                    return {};
                }
                current = ret;
                pending_symbol = nullptr;
                pending_function = nullptr;
                break;
            }
            case Expr::PostfixOp::Kind::Index: {
                TypeView idx = analyzeExpr(op.index.get());
                requireIntegral(idx, op.index.get(), "index expression");
                if (current.modifiers.empty() ||
                    (current.modifiers.back() != TypeModifier::Kind::Pointer &&
                     current.modifiers.back() != TypeModifier::Kind::Reference)) {
                    error(expr, "indexing requires pointer/reference-like value");
                    return {};
                }
                current.modifiers.pop_back();
                break;
            }
        }
    }
    return current;
}

bool SemanticAnalyzer::isLValue(Expr* expr) const {
    if (!expr) return false;
    if (expr->kind == Expr::Kind::Ident) return true;
    if (expr->kind == Expr::Kind::Postfix) {
        if (expr->postfix.ops.empty()) return false;
        const auto k = expr->postfix.ops.back().kind;
        return k == Expr::PostfixOp::Kind::MemberAccess ||
               k == Expr::PostfixOp::Kind::Arrow ||
               k == Expr::PostfixOp::Kind::SafeArrow ||
               k == Expr::PostfixOp::Kind::Index;
    }
    if (expr->kind == Expr::Kind::Unary && expr->unary.op == UnaryOp::Deref)
        return true;
    return false;
}

SemanticSymbol* SemanticAnalyzer::symbolForLValue(Expr* expr) const {
    if (!expr || expr->kind != Expr::Kind::Ident || !expr->ident) return nullptr;
    return resolveSymbol(qualifiedName(*expr->ident));
}

bool SemanticAnalyzer::checkReturn(Stmt& stmt) {
    if (function_stack_.empty()) {
        error(&stmt, "return outside function");
        return false;
    }
    if (function_stack_.back().is_noreturn) {
        error(&stmt, "noreturn function cannot return normally");
        return false;
    }
    TypeView expected = view(function_stack_.back().return_type);
    TypeView actual;
    if (stmt.return_values) actual = analyzeExpr(stmt.return_values.get());
    if (isVoid(expected)) {
        if (stmt.return_values)
            error(&stmt, "void function cannot return a value");
        return true;
    }
    if (!stmt.return_values) {
        error(&stmt, "non-void function must return a value");
        return false;
    }
    if (!isAssignable(expected, actual)) {
        error(&stmt, "return type mismatch: expected " + typeString(expected) +
            ", got " + typeString(actual));
        return false;
    }
    return true;
}

bool SemanticAnalyzer::checkThrow(Stmt& stmt) {
    if (function_stack_.empty()) {
        error(&stmt, "throw outside function");
        return false;
    }
    TypeView thrown = analyzeExpr(stmt.throw_stmt.expr.get());
    FunctionContext& ctx = function_stack_.back();
    if (!ctx.is_throws && !ctx.throws_type && ctx.catch_depth == 0) {
        error(&stmt, "function does not declare throws and there is no active catch");
        return false;
    }
    if (ctx.throws_type && !isAssignable(view(ctx.throws_type), thrown)) {
        error(&stmt, "thrown type " + typeString(thrown) +
            " does not match throws type " + typeString(ctx.throws_type));
        return false;
    }
    return true;
}

bool SemanticAnalyzer::checkResume(Stmt& stmt) {
    TypeView target = analyzeExpr(stmt.resume_stmt.target.get());
    if (!target.valid) return false;
    if (target.modifiers.empty() || target.modifiers.back() != TypeModifier::Kind::FuncPtr) {
        error(&stmt, "resume requires a coroutine function pointer/handle");
        return false;
    }
    return true;
}

bool SemanticAnalyzer::checkYield(Stmt& stmt) {
    if (function_stack_.empty() || !function_stack_.back().is_coroutine) {
        error(&stmt, "yield is only valid inside a coroutine");
        return false;
    }
    if (stmt.expr_stmt) analyzeExpr(stmt.expr_stmt.get());
    return true;
}

bool SemanticAnalyzer::checkDrop(Stmt& stmt) {
    TypeView value = analyzeExpr(stmt.drop_stmt.expr.get());
    if (!value.valid) return false;
    if (auto* s = symbolForLValue(stmt.drop_stmt.expr.get())) {
        if (s->state == SymbolState::Moved) {
            error(&stmt, "cannot drop moved value '" + s->name + "'");
            return false;
        }
        // SymbolState currently has Valid/Moved only. Reuse Moved as the
        // invalid-after-drop state so later use/move is rejected without
        // changing semantic_symbol.cpp/.hpp.
        s->state = SymbolState::Moved;
    }
    return true;
}

bool SemanticAnalyzer::checkWipe(Stmt& stmt) {
    TypeView value = analyzeExpr(stmt.wipe_stmt.expr.get());
    if (!value.valid) return false;
    return true;
}

bool SemanticAnalyzer::checkLabel(Stmt& stmt) {
    auto symbol = std::make_unique<SemanticSymbol>();
    symbol->name = stmt.label;
    symbol->kind = SymbolKind::Label;
    symbol->declaration = &stmt;
    symbol->is_defined = true;
    return declareSymbol(std::move(symbol), &stmt);
}

bool SemanticAnalyzer::checkJump(Stmt& stmt) {
    if (stmt.jump_target.empty()) {
        error(&stmt, "empty jump target");
        return false;
    }
    if (!symbols_.lookup(stmt.jump_target)) {
        error(&stmt, "unknown jump target '" + stmt.jump_target + "'");
        return false;
    }
    return true;
}
