#include <memory>
#include <vector>
#include <string>
#include <unordered_set>
#include <stdexcept>

class LoweringEngine {
private:
    Type* current_return_type = nullptr;
    std::unordered_set<std::string> throwing_functions; // Menyimpan daftar nama fungsi yang melempar error
    int err_var_counter = 0;

    std::string nextErrVarName() {
        return "__err_slot_" + std::to_string(err_var_counter++);
    }

    TypeModifier cloneTypeModifier(const TypeModifier& src) const {
        TypeModifier dst;
        dst.kind = src.kind;
        for (const auto& p : src.func_params) {
            dst.func_params.push_back(cloneType(p.get()));
        }
        dst.func_return = cloneType(src.func_return.get());
        dst.throws_type = cloneType(src.throws_type.get());
        return dst;
    }

    bool isPointerLikeType(const Type* type) const {
        if (!type) return false;
        if (!type->modifiers.empty()) {
            const auto& lastMod = type->modifiers.back();
            if (lastMod.kind == TypeModifier::Kind::Pointer ||
                lastMod.kind == TypeModifier::Kind::Reference) {
                return true;
            }
        }
        if (type->base && !type->base->parts.empty()) {
            const std::string& baseName = type->base->parts.back();
            if (baseName == "ptr") return true;
        }
        return false;
    }
    int coalesce_counter = 0;

std::string nextCoalesceVarName() {
    return "__coalesce_" + std::to_string(coalesce_counter++);
}

void collectAndLowerCoalesceInExpr(Expr* expr, std::vector<std::unique_ptr<Stmt>>& prefixStmts) {
    if (!expr) return;

    switch (expr->kind) {
        case Expr::Kind::Unary:
            collectAndLowerCoalesceInExpr(expr->unary.expr.get(), prefixStmts);
            break;
        case Expr::Kind::Try:
            collectAndLowerCoalesceInExpr(expr->try_expr.expr.get(), prefixStmts);
            break;
        case Expr::Kind::Group:
            collectAndLowerCoalesceInExpr(expr->group.expr.get(), prefixStmts);
            break;
        case Expr::Kind::Cast:
            collectAndLowerCoalesceInExpr(expr->cast.base.get(), prefixStmts);
            break;
        case Expr::Kind::Assign:
            collectAndLowerCoalesceInExpr(expr->assign.lhs.get(), prefixStmts);
            collectAndLowerCoalesceInExpr(expr->assign.rhs.get(), prefixStmts);
            break;
        case Expr::Kind::Ternary:
            collectAndLowerCoalesceInExpr(expr->ternary.cond.get(), prefixStmts);
            collectAndLowerCoalesceInExpr(expr->ternary.then_expr.get(), prefixStmts);
            collectAndLowerCoalesceInExpr(expr->ternary.else_expr.get(), prefixStmts);
            break;
        case Expr::Kind::Postfix:
            collectAndLowerCoalesceInExpr(expr->postfix.base.get(), prefixStmts);
            for (auto& op : expr->postfix.ops) {
                for (auto& a : op.args) collectAndLowerCoalesceInExpr(a.get(), prefixStmts);
                if (op.index) collectAndLowerCoalesceInExpr(op.index.get(), prefixStmts);
            }
            break;
        case Expr::Kind::Binary: {
            // Turunkan anak-anaknya dulu (termasuk coalesce di rhs atau nested di lhs)
            collectAndLowerCoalesceInExpr(expr->binary.lhs.get(), prefixStmts);
            collectAndLowerCoalesceInExpr(expr->binary.rhs.get(), prefixStmts);

            if (expr->binary.op == BinaryOp::Coalesce) {
                auto lhs = std::move(expr->binary.lhs);
                auto rhs = std::move(expr->binary.rhs);

                // Kasus sederhana: null ?? rhs -> langsung rhs
                if (lhs && lhs->kind == Expr::Kind::Literal &&
                    lhs->literal && lhs->literal->kind == Literal::Kind::Null) {
                    if (rhs) {
                        *expr = std::move(*rhs);
                    }
                    return;
                }

                std::string varName = nextCoalesceVarName();

                // 1. ptr __coalesce_N = <lhs>;
                VarDecl decl;
                auto ptrType = std::make_unique<Type>();
                ptrType->base = std::make_unique<QualifiedName>();
                ptrType->base->parts.push_back("ptr");
                decl.type = std::move(ptrType);

                VarDecl::Item item;
                item.name = varName;
                item.init = std::move(lhs);
                decl.items.push_back(std::move(item));

                auto declStmt = std::make_unique<Stmt>();
                declStmt->kind = Stmt::Kind::VarDecl;
                declStmt->var_decl = std::move(decl);
                prefixStmts.push_back(std::move(declStmt));

                // 2. if (__coalesce_N == null) { __coalesce_N = <rhs>; }
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;
                assignExpr->assign.lhs = makeIdentExpr(varName);
                assignExpr->assign.rhs = std::move(rhs);

                auto assignStmt = std::make_unique<Stmt>();
                assignStmt->kind = Stmt::Kind::ExprStmt;
                assignStmt->expr_stmt = std::move(assignExpr);

                auto thenBlock = std::make_unique<Stmt>();
                thenBlock->kind = Stmt::Kind::Block;
                thenBlock->block.stmts.push_back(std::move(assignStmt));

                auto condExpr = std::make_unique<Expr>();
                condExpr->kind = Expr::Kind::Binary;
                condExpr->binary.op = BinaryOp::Eq;
                condExpr->binary.lhs = makeIdentExpr(varName);
                condExpr->binary.rhs = makeNullLiteral();

                auto ifStmt = std::make_unique<Stmt>();
                ifStmt->kind = Stmt::Kind::If;
                ifStmt->if_stmt.cond = std::move(condExpr);
                ifStmt->if_stmt.then_block = std::move(thenBlock);

                prefixStmts.push_back(std::move(ifStmt));

                // 3. Ganti ekspresi saat ini menjadi __coalesce_N
                auto replacement = makeIdentExpr(varName);
                *expr = std::move(*replacement);
            }
            break;
        }
        default:
            break;
    }
}


std::unique_ptr<Expr> cloneExpr(const Expr* src) const {
    if (!src) return nullptr;

    auto dst = std::make_unique<Expr>();
    dst->kind = src->kind;
    dst->file = src->file;
    dst->line_start = src->line_start;
    dst->column_start = src->column_start;
    dst->line_end = src->line_end;
    dst->column_end = src->column_end;
    dst->array_size = src->array_size;

    if (src->literal) {
        dst->literal = std::make_unique<Literal>();
        dst->literal->kind = src->literal->kind;
        dst->literal->value = src->literal->value;
    }

    if (src->ident) {
        dst->ident = std::make_unique<QualifiedName>();
        dst->ident->parts = src->ident->parts;
        dst->ident->is_absolute = src->ident->is_absolute;
    }

    dst->unary.op = src->unary.op;
    dst->unary.expr = cloneExpr(src->unary.expr.get());

    dst->try_expr.expr = cloneExpr(src->try_expr.expr.get());

    dst->binary.op = src->binary.op;
    dst->binary.lhs = cloneExpr(src->binary.lhs.get());
    dst->binary.rhs = cloneExpr(src->binary.rhs.get());

    dst->group.expr = cloneExpr(src->group.expr.get());

    dst->assign.op = src->assign.op;
    dst->assign.lhs = cloneExpr(src->assign.lhs.get());
    dst->assign.rhs = cloneExpr(src->assign.rhs.get());

    dst->ternary.cond = cloneExpr(src->ternary.cond.get());
    dst->ternary.then_expr = cloneExpr(src->ternary.then_expr.get());
    dst->ternary.else_expr = cloneExpr(src->ternary.else_expr.get());

    dst->intrinsic.name = src->intrinsic.name;
    dst->intrinsic.type = cloneType(src->intrinsic.type.get());
    for (const auto& a : src->intrinsic.args) {
        dst->intrinsic.args.push_back(cloneExpr(a.get()));
    }

    dst->postfix.base = cloneExpr(src->postfix.base.get());
    for (const auto& op : src->postfix.ops) {
        Expr::PostfixOp copiedOp;
        copiedOp.kind = op.kind;
        copiedOp.name = op.name;
        copiedOp.index = cloneExpr(op.index.get());
        for (const auto& a : op.args) {
            copiedOp.args.push_back(cloneExpr(a.get()));
        }
        dst->postfix.ops.push_back(std::move(copiedOp));
    }

    dst->cast.base = cloneExpr(src->cast.base.get());
    dst->cast.target = cloneType(src->cast.target.get());

    if (src->new_expr.type) {
        dst->new_expr.type = std::make_unique<QualifiedName>();
        dst->new_expr.type->parts = src->new_expr.type->parts;
        dst->new_expr.type->is_absolute = src->new_expr.type->is_absolute;
    }
    for (const auto& a : src->new_expr.args) {
        dst->new_expr.args.push_back(cloneExpr(a.get()));
    }
    for (const auto& d : src->new_expr.array_dims) {
        dst->new_expr.array_dims.push_back(cloneExpr(d.get()));
    }
    if (src->new_expr.alloc_size) {
        dst->new_expr.alloc_size = std::make_unique<Literal>();
        dst->new_expr.alloc_size->kind = src->new_expr.alloc_size->kind;
        dst->new_expr.alloc_size->value = src->new_expr.alloc_size->value;
    }

    dst->struct_init.type_name = cloneType(src->struct_init.type_name.get());
    for (const auto& f : src->struct_init.fields) {
        dst->struct_init.fields.push_back({f.first, cloneExpr(f.second.get())});
    }

    for (const auto& item : src->array_items) {
        dst->array_items.push_back(cloneExpr(item.get()));
    }

    return dst;
}

    int label_counter = 0;
    bool consumeTryAndInjectSlot(Expr* expr, const std::string& slotName) {
        if (!expr) return false;

        if (expr->kind == Expr::Kind::Try) {
            if (!expr->try_expr.expr) return false;
            auto inner = std::move(expr->try_expr.expr);
            *expr = std::move(*inner);
            injectErrSlotInExpr(expr, slotName);
            return true;
        }

        switch (expr->kind) {
            case Expr::Kind::Unary:
                return consumeTryAndInjectSlot(expr->unary.expr.get(), slotName);
            case Expr::Kind::Binary:
                return consumeTryAndInjectSlot(expr->binary.lhs.get(), slotName) ||
                       consumeTryAndInjectSlot(expr->binary.rhs.get(), slotName);
            case Expr::Kind::Assign:
                return consumeTryAndInjectSlot(expr->assign.lhs.get(), slotName) ||
                       consumeTryAndInjectSlot(expr->assign.rhs.get(), slotName);
            case Expr::Kind::Ternary:
                return consumeTryAndInjectSlot(expr->ternary.cond.get(), slotName) ||
                       consumeTryAndInjectSlot(expr->ternary.then_expr.get(), slotName) ||
                       consumeTryAndInjectSlot(expr->ternary.else_expr.get(), slotName);
            case Expr::Kind::Cast:
                return consumeTryAndInjectSlot(expr->cast.base.get(), slotName);
            default:
                return false;
        }
    }

    bool tryHandleStmtInDo(Stmt* s, const std::string& slotName) {
        if (!s) return false;
        if (s->kind == Stmt::Kind::ExprStmt) {
            return consumeTryAndInjectSlot(s->expr_stmt.get(), slotName);
        }
        if (s->kind == Stmt::Kind::VarDecl) {
            bool found = false;
            for (auto& it : s->var_decl.items) {
                if (consumeTryAndInjectSlot(it.init.get(), slotName)) found = true;
                for (auto& a : it.args) {
                    if (consumeTryAndInjectSlot(a.get(), slotName)) found = true;
                }
            }
            return found;
        }
        if (s->kind == Stmt::Kind::Return) {
            return consumeTryAndInjectSlot(s->return_values.get(), slotName);
        }
        return false;
    }

    // --- Helper rekursif untuk menyuntikkan &__err_slot_N ke call throwing_functions ---
void injectErrSlotInExpr(Expr* expr, const std::string& slotName) {
    if (!expr) return;

    switch (expr->kind) {
        case Expr::Kind::Postfix: {
            injectErrSlotInExpr(expr->postfix.base.get(), slotName);

            bool isThrowingCall = false;
            if (expr->postfix.base &&
                expr->postfix.base->kind == Expr::Kind::Ident &&
                expr->postfix.base->ident &&
                !expr->postfix.base->ident->parts.empty()) {
                isThrowingCall = throwing_functions.count(expr->postfix.base->ident->parts.back()) > 0;
            }

            for (auto& op : expr->postfix.ops) {
                for (auto& arg : op.args) {
                    injectErrSlotInExpr(arg.get(), slotName);
                }
                if (op.index) {
                    injectErrSlotInExpr(op.index.get(), slotName);
                }
                if (op.kind == Expr::PostfixOp::Kind::Call && isThrowingCall) {
                    op.args.push_back(makeRefExpr(slotName));
                    isThrowingCall = false;
                }  
            }

            break;
        }
        case Expr::Kind::Unary:
            injectErrSlotInExpr(expr->unary.expr.get(), slotName);
            break;
        case Expr::Kind::Binary:
            injectErrSlotInExpr(expr->binary.lhs.get(), slotName);
            injectErrSlotInExpr(expr->binary.rhs.get(), slotName);
            break;
        case Expr::Kind::Assign:
            injectErrSlotInExpr(expr->assign.lhs.get(), slotName);
            injectErrSlotInExpr(expr->assign.rhs.get(), slotName);
            break;
        case Expr::Kind::Ternary:
            injectErrSlotInExpr(expr->ternary.cond.get(), slotName);
            injectErrSlotInExpr(expr->ternary.then_expr.get(), slotName);
            injectErrSlotInExpr(expr->ternary.else_expr.get(), slotName);
            break;
        case Expr::Kind::Cast:
            injectErrSlotInExpr(expr->cast.base.get(), slotName);
            break;
        default:
            break;
    }
}

void injectErrSlotInStmt(Stmt* s, const std::string& slotName) {
    if (!s) return;

    switch (s->kind) {
        case Stmt::Kind::Block:
            for (auto& child : s->block.stmts) {
                injectErrSlotInStmt(child.get(), slotName);
            }
            break;
        case Stmt::Kind::ExprStmt:
            injectErrSlotInExpr(s->expr_stmt.get(), slotName);
            break;
        case Stmt::Kind::VarDecl:
            for (auto& it : s->var_decl.items) {
                injectErrSlotInExpr(it.init.get(), slotName);
                for (auto& a : it.args) {
                    injectErrSlotInExpr(a.get(), slotName);
                }
            }
            break;
        case Stmt::Kind::Return:
            injectErrSlotInExpr(s->return_values.get(), slotName);
            break;
        default:
            break;
    }
}

    std::unique_ptr<Type> cloneType(const Type* src) const {
        if (!src) return nullptr;
        auto dst = std::make_unique<Type>();
        dst->file = src->file;
        dst->line_start = src->line_start;
        dst->column_start = src->column_start;
        dst->is_array = src->is_array;
        dst->unsized_array = src->unsized_array;
        dst->qualifiers = src->qualifiers;

        for (const auto& mod : src->modifiers) {
            dst->modifiers.push_back(cloneTypeModifier(mod));
        }

        if (src->base) {
            auto newBase = std::make_unique<QualifiedName>();
            newBase->parts = src->base->parts;
            newBase->is_absolute = src->base->is_absolute;
            dst->base = std::move(newBase);
        }
        return dst;
    }

    std::unique_ptr<Expr> makeIdentExpr(const std::string& name) const {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Ident;
        expr->ident = std::make_unique<QualifiedName>();
        expr->ident->parts.push_back(name);
        return expr;
    }

    std::unique_ptr<Expr> makeRefExpr(const std::string& name) const {
        auto refExpr = std::make_unique<Expr>();
        refExpr->kind = Expr::Kind::Unary;
        refExpr->unary.op = UnaryOp::Ref;
        refExpr->unary.expr = makeIdentExpr(name);
        return refExpr;
    }

    std::unique_ptr<Expr> makeNullLiteral() const {
        auto nullLit = std::make_unique<Expr>();
        nullLit->kind = Expr::Kind::Literal;
        nullLit->literal = std::make_unique<Literal>();
        nullLit->literal->kind = Literal::Kind::Null;
        nullLit->literal->value = "null";
        return nullLit;
    }

public:
    void lowerProgram(FileProgram* program) {
        if (!program) return;

        // Pass 1: Kumpulkan semua fungsi yang memiliki klausa throws
        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                if (func->is_throws && func->name && !func->name->parts.empty()) {
                    throwing_functions.insert(func->name->parts.back());
                }
            } else if (auto ns = dynamic_cast<NamespaceDecl*>(node.get())) {
                collectNamespaceSymbols(ns, "");
            }
        }

        std::vector<std::unique_ptr<Node>> nextDecls;
        for (auto& node : program->decls) {
            if (auto ns = dynamic_cast<NamespaceDecl*>(node.get())) {
                flattenNodeWithNs(ns, nextDecls);
            } else {
                flattenNodeWithNs(node.get(), nextDecls);
                nextDecls.push_back(std::move(node));
            }
        }
        program->decls = std::move(nextDecls);

        // Pass 2: Lowering deklarasi (fungsi, dll)
        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                lowerFunction(func);
            }
        }
    }

private:

        // --- Tambah di private LoweringEngine ---
    struct NamespaceContext {
    std::string prefix;
    std::unordered_set<std::string> symbols;
};

std::vector<NamespaceContext> ns_stack_;
std::unordered_map<std::string, std::unordered_set<std::string>> ns_all_symbols_;
std::vector<std::unordered_set<std::string>> local_scopes_;

std::string currentNsPrefix() const {
    if (ns_stack_.empty()) return "";
    std::string p = ns_stack_[0].prefix;
    for (size_t i = 1; i < ns_stack_.size(); ++i) {
        p += "_" + ns_stack_[i].prefix;
    }
    return p;
}

std::string qualifyNsName(const std::string& name) const {
    std::string p = currentNsPrefix();
    return p.empty() ? name : (p + "_" + name);
}

bool isShadowedByLocal(const std::string& name) const {
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
        if (it->count(name)) return true;
    }
    return false;
}

std::string resolveUnqualifiedInNs(const std::string& name) const {
    if (isShadowedByLocal(name)) return name;
    for (auto it = ns_stack_.rbegin(); it != ns_stack_.rend(); ++it) {
        if (it->symbols.count(name)) {
            return it->prefix + "_" + name;
        }
    }
    return name;
}

std::string resolveQualifiedParts(const std::vector<std::string>& parts) const {
    if (parts.empty()) return "";
    if (parts.size() == 1) {
        return resolveUnqualifiedInNs(parts[0]);
    }
    std::string joined = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        joined += "_" + parts[i];
    }
    std::string cur = currentNsPrefix();
    if (!cur.empty()) {
        std::string curCandidate = cur + "_" + joined;
        if (ns_all_symbols_.count(parts[0]) && ns_all_symbols_.at(parts[0]).count(parts.back())) {
            return curCandidate;
        }
    }
    return joined;
}

void collectNamespaceSymbols(NamespaceDecl* ns, const std::string& fullPrefix) {
    if (!ns) return;
    std::string nsName = (ns->name && !ns->name->parts.empty()) ? ns->name->parts.back() : "anon";
    std::string thisPrefix = fullPrefix.empty() ? nsName : (fullPrefix + "_" + nsName);
    auto& symSet = ns_all_symbols_[thisPrefix];

    for (auto& item : ns->items) {
        if (auto fn = dynamic_cast<Function*>(item.get())) {
            if (fn->name && !fn->name->parts.empty()) symSet.insert(fn->name->parts.back());
        } else if (auto st = dynamic_cast<StructDef*>(item.get())) {
            if (st->name && !st->name->parts.empty()) symSet.insert(st->name->parts.back());
        } else if (auto un = dynamic_cast<UnionDef*>(item.get())) {
            if (un->name && !un->name->parts.empty()) symSet.insert(un->name->parts.back());
        } else if (auto en = dynamic_cast<EnumDef*>(item.get())) {
            if (en->name && !en->name->parts.empty()) symSet.insert(en->name->parts.back());
        } else if (auto vd = dynamic_cast<VarDecl*>(item.get())) {
            for (const auto& it : vd->items) symSet.insert(it.name);
        } else if (auto childNs = dynamic_cast<NamespaceDecl*>(item.get())) {
            if (childNs->name && !childNs->name->parts.empty()) {
                symSet.insert(childNs->name->parts.back());
                collectNamespaceSymbols(childNs, thisPrefix);
            }
        }
    }
}

void resolveTypeNs(Type* t) {
    if (!t) return;
    if (t->base && !t->base->parts.empty()) {
        std::string resolved = resolveQualifiedParts(t->base->parts);
        t->base->parts = {resolved};
        t->base->is_absolute = false;
    }
    for (auto& mod : t->modifiers) {
        for (auto& p : mod.func_params) resolveTypeNs(p.get());
        resolveTypeNs(mod.func_return.get());
        resolveTypeNs(mod.throws_type.get());
    }
}

void resolveExprNs(Expr* expr) {
    if (!expr) return;
    switch (expr->kind) {
        case Expr::Kind::Ident: {
            if (expr->ident && !expr->ident->parts.empty()) {
                if (expr->ident->parts.size() == 1 && expr->ident->is_absolute) {
                    break;
                }
                std::string resolved = resolveQualifiedParts(expr->ident->parts);
                expr->ident->parts = {resolved};
                expr->ident->is_absolute = false;
            }
            break;
        }
        case Expr::Kind::Postfix: {
            // Tangani bentuk Data::data atau Data::func() bila di-parse sebagai Postfix
            if (expr->postfix.base &&
                expr->postfix.base->kind == Expr::Kind::Ident &&
                expr->postfix.base->ident &&
                expr->postfix.base->ident->parts.size() == 1) {
                const std::string& firstPart = expr->postfix.base->ident->parts[0];
                if (!isShadowedByLocal(firstPart) && ns_all_symbols_.count(firstPart) &&
                    !expr->postfix.ops.empty()) {
                    const auto& firstOp = expr->postfix.ops[0];
                    if ((firstOp.kind == Expr::PostfixOp::Kind::MemberAccess ||
                         firstOp.kind == Expr::PostfixOp::Kind::Call) &&
                        !firstOp.name.empty() &&
                        ns_all_symbols_[firstPart].count(firstOp.name)) {
                        std::string mangled = firstPart + "_" + firstOp.name;
                        expr->postfix.base->ident->parts = {mangled};
                        if (firstOp.kind == Expr::PostfixOp::Kind::MemberAccess) {
                            expr->postfix.ops.erase(expr->postfix.ops.begin());
                            if (expr->postfix.ops.empty()) {
                                auto identExpr = std::move(expr->postfix.base);
                                *expr = std::move(*identExpr);
                                break;
                            }
                        } else if (firstOp.kind == Expr::PostfixOp::Kind::Call) {
                            // Biarkan op Call tetap ada di ops[0], nama fungsinya sudah jadi Data_func
                        }
                    }
                }
            }
            resolveExprNs(expr->postfix.base.get());
            for (auto& op : expr->postfix.ops) {
                for (auto& a : op.args) resolveExprNs(a.get());
                if (op.index) resolveExprNs(op.index.get());
            }
            break;
        }
        case Expr::Kind::Unary:
            resolveExprNs(expr->unary.expr.get());
            break;
        case Expr::Kind::Try:
            resolveExprNs(expr->try_expr.expr.get());
            break;
        case Expr::Kind::Group:
            resolveExprNs(expr->group.expr.get());
            break;
        case Expr::Kind::Cast:
            resolveTypeNs(expr->cast.target.get());
            resolveExprNs(expr->cast.base.get());
            break;
        case Expr::Kind::Binary:
            resolveExprNs(expr->binary.lhs.get());
            resolveExprNs(expr->binary.rhs.get());
            break;
        case Expr::Kind::Assign:
            resolveExprNs(expr->assign.lhs.get());
            resolveExprNs(expr->assign.rhs.get());
            break;
        case Expr::Kind::Ternary:
            resolveExprNs(expr->ternary.cond.get());
            resolveExprNs(expr->ternary.then_expr.get());
            resolveExprNs(expr->ternary.else_expr.get());
            break;
        case Expr::Kind::StructInit:
            resolveTypeNs(expr->struct_init.type_name.get());
            for (auto& f : expr->struct_init.fields) resolveExprNs(f.second.get());
            break;
        case Expr::Kind::ArrayLiteral:
            for (auto& item : expr->array_items) resolveExprNs(item.get());
            break;
        case Expr::Kind::Intrinsic:
            resolveTypeNs(expr->intrinsic.type.get());
            for (auto& a : expr->intrinsic.args) resolveExprNs(a.get());
            break;
        default:
            break;
    }
}

void resolveStmtNs(Stmt* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Block:
            local_scopes_.push_back({});
            for (auto& s : stmt->block.stmts) resolveStmtNs(s.get());
            local_scopes_.pop_back();
            break;
        case Stmt::Kind::VarDecl:
            resolveTypeNs(stmt->var_decl.type.get());
            for (auto& it : stmt->var_decl.items) {
                resolveExprNs(it.init.get());
                for (auto& a : it.args) resolveExprNs(a.get());
                if (!local_scopes_.empty()) local_scopes_.back().insert(it.name);
            }
            break;
        case Stmt::Kind::Return:
            resolveExprNs(stmt->return_values.get());
            break;
        case Stmt::Kind::ExprStmt:
            resolveExprNs(stmt->expr_stmt.get());
            break;
        case Stmt::Kind::If:
            resolveExprNs(stmt->if_stmt.cond.get());
            if (stmt->if_stmt.then_block) resolveStmtNs(stmt->if_stmt.then_block.get());
            for (auto& elif : stmt->if_stmt.elifs) {
                resolveExprNs(elif.cond.get());
                if (elif.block) resolveStmtNs(elif.block.get());
            }
            if (stmt->if_stmt.else_block) resolveStmtNs(stmt->if_stmt.else_block.get());
            break;
        case Stmt::Kind::While:
            resolveExprNs(stmt->while_stmt.cond.get());
            if (stmt->while_stmt.body) resolveStmtNs(stmt->while_stmt.body.get());
            break;
        case Stmt::Kind::DoStmt:
            if (stmt->do_stmt.body) resolveStmtNs(stmt->do_stmt.body.get());
            if (stmt->do_stmt.cond) resolveExprNs(stmt->do_stmt.cond.get());
            if (stmt->do_stmt.catch_body) resolveStmtNs(stmt->do_stmt.catch_body.get());
            break;
        case Stmt::Kind::SwitchCase:
            resolveExprNs(stmt->switch_stmt.expr.get());
            for (auto& c : stmt->switch_stmt.cases) {
                for (auto& v : c.values) resolveExprNs(v.get());
                if (c.body) resolveStmtNs(c.body.get());
            }
            break;
        case Stmt::Kind::Unsafe:
            if (stmt->unsafe_stmt.stmt) resolveStmtNs(stmt->unsafe_stmt.stmt.get());
            break;
        case Stmt::Kind::ThrowStmt:
            resolveExprNs(stmt->throw_stmt.expr.get());
            break;
        default:
            break;
    }
}

void flattenNodeWithNs(Node* node, std::vector<std::unique_ptr<Node>>& outDecls) {
    if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
        std::string nsName = (ns->name && !ns->name->parts.empty()) ? ns->name->parts.back() : "anon";
        std::string fullPrefix = qualifyNsName(nsName);
        ns_stack_.push_back({nsName, ns_all_symbols_[fullPrefix]});

        for (auto& item : ns->items) {
            flattenNodeWithNs(item.get(), outDecls);
            if (item) {
                outDecls.push_back(std::move(item));
            }
        }

        ns_stack_.pop_back();
        return;
    }

    if (auto fn = dynamic_cast<Function*>(node)) {
        if (fn->name && !fn->name->parts.empty()) {
            std::string rawName = fn->name->parts.back();
            std::string qName = qualifyNsName(rawName);
            fn->name->parts = {qName};
            if (fn->is_throws) {
                throwing_functions.insert(qName);
            }
        }
        resolveTypeNs(fn->return_types.get());
        resolveTypeNs(fn->throws_type.get());
        local_scopes_.clear();
        local_scopes_.push_back({});
        for (auto& p : fn->params) {
            resolveTypeNs(p.type.get());
            resolveExprNs(p.init.get());
            local_scopes_.back().insert(p.name);
        }
        if (fn->body) resolveStmtNs(fn->body.get());
        local_scopes_.clear();
    } else if (auto st = dynamic_cast<StructDef*>(node)) {
        if (st->name && !st->name->parts.empty()) {
            st->name->parts = {qualifyNsName(st->name->parts.back())};
        }
        for (auto& mem : st->members) {
            if (auto vd = dynamic_cast<VarDecl*>(mem.get())) {
                resolveTypeNs(vd->type.get());
                for (auto& it : vd->items) resolveExprNs(it.init.get());
            } else if (auto mfn = dynamic_cast<Function*>(mem.get())) {
                if (mfn->name && !mfn->name->parts.empty()) {
                    std::string mName = mfn->name->parts.back();
                    if (!currentNsPrefix().empty()) {
                        mfn->name->parts = {qualifyNsName(mName)};
                    }
                }
                resolveTypeNs(mfn->return_types.get());
                local_scopes_.clear();
                local_scopes_.push_back({});
                for (auto& p : mfn->params) {
                    resolveTypeNs(p.type.get());
                    local_scopes_.back().insert(p.name);
                }
                if (mfn->body) resolveStmtNs(mfn->body.get());
                local_scopes_.clear();
            }
        }
    } else if (auto un = dynamic_cast<UnionDef*>(node)) {
        if (un->name && !un->name->parts.empty()) {
            un->name->parts = {qualifyNsName(un->name->parts.back())};
        }
        for (auto& f : un->fields) resolveTypeNs(f.type.get());
    } else if (auto en = dynamic_cast<EnumDef*>(node)) {
        if (en->name && !en->name->parts.empty()) {
            en->name->parts = {qualifyNsName(en->name->parts.back())};
        }
        for (auto& it : en->items) resolveExprNs(it.value.get());
    } else if (auto vd = dynamic_cast<VarDecl*>(node)) {
        resolveTypeNs(vd->type.get());
        for (auto& it : vd->items) {
            if (!currentNsPrefix().empty()) {
                it.name = qualifyNsName(it.name);
                ns_vars_.insert(it.name);
            }
            resolveExprNs(it.init.get());
            for (auto& a : it.args) resolveExprNs(a.get());
        }
    }
}

    void lowerExpr(Expr* expr) {
        if (!expr) return;

        switch (expr->kind) {
            case Expr::Kind::Ident: {
            if (expr->ident && !expr->ident->parts.empty()) {
        // Jika sudah is_absolute dengan 1 bagian, pertahankan hanya jika memang ns_var;
        // kalau bukan ns_var, biarkan tetap true bila memang ::global asli.
                if (expr->ident->parts.size() == 1 && expr->ident->is_absolute) {
                    break;
                }
                std::string resolved = resolveQualifiedParts(expr->ident->parts);
                bool isNsVar = !isShadowedByLocal(expr->ident->parts.back()) && isNamespaceVar(resolved);
                expr->ident->parts = {resolved};
                expr->ident->is_absolute = isNsVar;
                }
            break;
            }
            case Expr::Kind::Postfix: {
                lowerExpr(expr->postfix.base.get());

                bool isThrowingCall = false;
                if (expr->postfix.base &&
                    expr->postfix.base->kind == Expr::Kind::Ident &&
                    expr->postfix.base->ident &&
                    !expr->postfix.base->ident->parts.empty()) {
                    isThrowingCall = throwing_functions.count(expr->postfix.base->ident->parts.back()) > 0;
                }

                for (auto& op : expr->postfix.ops) {
                    for (auto& arg : op.args) {
                        lowerExpr(arg.get());
                    }
                    if (op.index) {
                        lowerExpr(op.index.get());
                    }
                    if (op.kind == Expr::PostfixOp::Kind::Call && isThrowingCall) {
                        op.args.push_back(makeNullLiteral());
                        isThrowingCall = false;
                    }
                }
                break;
            }
            case Expr::Kind::Unary:
                lowerExpr(expr->unary.expr.get());
                break;
            case Expr::Kind::Try: {
                if (expr->try_expr.expr) {
                    auto inner = std::move(expr->try_expr.expr);
                    lowerExpr(inner.get());
                    *expr = std::move(*inner);
                }
                break;
            }

            case Expr::Kind::Binary:
                lowerExpr(expr->binary.lhs.get());
                lowerExpr(expr->binary.rhs.get());
                break;
            case Expr::Kind::Assign:
                lowerExpr(expr->assign.lhs.get());
                lowerExpr(expr->assign.rhs.get());
                break;
            case Expr::Kind::Ternary:
                lowerExpr(expr->ternary.cond.get());
                lowerExpr(expr->ternary.then_expr.get());
                lowerExpr(expr->ternary.else_expr.get());
                break;
            case Expr::Kind::Cast:
                lowerExpr(expr->cast.base.get());
                break;
            default:
                break;
        }
    }

    void lowerFunction(Function* func) {
        // Cek apakah fungsi ini dulunya adalah fungsi throws (gunakan pengecekan awal sebelum di-reset)
        bool was_throws = func->is_throws && func->throws_type;

        if (was_throws) {
            // 1. Buat parameter tambahan out-parameter (pointer)
            Function::Param errorParam;
            errorParam.type = std::move(func->throws_type);

            TypeModifier mod;
            mod.kind = TypeModifier::Kind::Pointer;
            errorParam.type->modifiers.push_back(std::move(mod));

            errorParam.name = "__throwed";
            errorParam.init = nullptr;

            func->params.push_back(std::move(errorParam));
        }

        current_return_type = func->return_types.get();

        // 2. Transformasi body fungsi (Lowering semua statement di dalam body)
        if (func->body) {
            lowerStmt(func->body.get());
        }

        if (was_throws) {
            func->is_throws = false;
            func->throws_type = nullptr;
        }
        current_return_type = nullptr;
    }

    std::unique_ptr<Expr> createDefaultReturnExpr(Type* type) {
        if (!type || !type->base || type->base->parts.empty()) {
            return nullptr;
        }

        const std::string& typeName = type->base->parts.back();
        if(typeName == "void") return nullptr;

        if (typeName == "int" || typeName == "i32" || typeName == "i64" ||
            typeName == "u32" || typeName == "u64" || typeName == "byte" || typeName == "char") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Number;
            expr->literal->value = "0";
            return expr;
        }

        if (typeName == "float" || typeName == "double" || typeName == "f32" || typeName == "f64") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Float;
            expr->literal->value = "0.0";
            return expr;
        }

        if (typeName == "bool") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::False;
            expr->literal->value = "false";
            return expr;
        }

        // Struct / Object Zero Init
        auto structExpr = std::make_unique<Expr>();
        structExpr->kind = Expr::Kind::StructInit;

        auto copiedType = std::make_unique<Type>();
        if (type && type->base) {
            copiedType->file = type->file;
            copiedType->line_start = type->line_start;
            copiedType->column_start = type->column_start;

            auto newBase = std::make_unique<QualifiedName>();
            newBase->parts = type->base->parts;
            copiedType->base = std::move(newBase);
        }

        structExpr->struct_init.type_name = std::move(copiedType);
        structExpr->struct_init.fields.clear();
        return structExpr;
    }

    void lowerStmt(Stmt* stmt) {
        if (!stmt) return;

        switch (stmt->kind) {
             case Stmt::Kind::ExprStmt: {
                lowerExpr(stmt->expr_stmt.get());
                std::vector<std::unique_ptr<Stmt>> prefix;
                collectAndLowerCoalesceInExpr(stmt->expr_stmt.get(), prefix);
                if (!prefix.empty()) {
                    auto blk = std::make_unique<Stmt>();
                    blk->kind = Stmt::Kind::Block;
                    for (auto& p : prefix) blk->block.stmts.push_back(std::move(p));
                    auto selfCopy = std::make_unique<Stmt>();
                    selfCopy->kind = Stmt::Kind::ExprStmt;
                    selfCopy->expr_stmt = std::move(stmt->expr_stmt);
                    blk->block.stmts.push_back(std::move(selfCopy));
                    *stmt = std::move(*blk);
                }
                break;
            }

            case Stmt::Kind::VarDecl: {
                std::vector<std::unique_ptr<Stmt>> prefix;
                for (auto& it : stmt->var_decl.items) {
                    lowerExpr(it.init.get());
                    collectAndLowerCoalesceInExpr(it.init.get(), prefix);
                    for (auto& a : it.args) {
                        lowerExpr(a.get());
                        collectAndLowerCoalesceInExpr(a.get(), prefix);
                    }
                }
                if (!prefix.empty()) {
                    auto blk = std::make_unique<Stmt>();
                    blk->kind = Stmt::Kind::Block;
                    for (auto& p : prefix) blk->block.stmts.push_back(std::move(p));
                    auto declStmt = std::make_unique<Stmt>();
                    declStmt->kind = Stmt::Kind::VarDecl;
                    declStmt->var_decl = std::move(stmt->var_decl);
                    blk->block.stmts.push_back(std::move(declStmt));
                    *stmt = std::move(*blk);
                }
                break;
            }

            case Stmt::Kind::Return: {
                lowerExpr(stmt->return_values.get());
                std::vector<std::unique_ptr<Stmt>> prefix;
                collectAndLowerCoalesceInExpr(stmt->return_values.get(), prefix);
                if (!prefix.empty()) {
                    auto blk = std::make_unique<Stmt>();
                    blk->kind = Stmt::Kind::Block;
                    for (auto& p : prefix) blk->block.stmts.push_back(std::move(p));
                    auto retStmt = std::make_unique<Stmt>();
                    retStmt->kind = Stmt::Kind::Return;
                    retStmt->return_values = std::move(stmt->return_values);
                    blk->block.stmts.push_back(std::move(retStmt));
                    *stmt = std::move(*blk);
                }
                break;
            }
            case Stmt::Kind::Block: {
                for (auto& s : stmt->block.stmts) {
                    lowerStmt(s.get());
                }
                break;
            }

            case Stmt::Kind::If: {
                if (stmt->if_stmt.then_block) {
                    lowerStmt(stmt->if_stmt.then_block.get());
                }
                for (auto& elif : stmt->if_stmt.elifs) {
                    if (elif.block) lowerStmt(elif.block.get());
                }
                if (stmt->if_stmt.else_block) {
                    lowerStmt(stmt->if_stmt.else_block.get());
                }
                break;
            }

            case Stmt::Kind::While: {
                if (stmt->while_stmt.body) lowerStmt(stmt->while_stmt.body.get());
                break;
            }

            // --- TRANSFORMASI UTAMA UNTUK 'do ... catch' ---
                        // --- TRANSFORMASI UTAMA UNTUK 'do ... catch' ---
            case Stmt::Kind::DoStmt: {
                if (stmt->do_stmt.iscatch) {
                    std::string slotName = "__err_slot_" + std::to_string(err_var_counter++);
                    std::string initVarName = slotName + "_init";
                    std::string catchLabel = "__catch_" + std::to_string(label_counter);
                    std::string endLabel = "__do_end_" + std::to_string(label_counter++);

                    Type* errTypePtr = stmt->do_stmt.error_var.type.get();
                    bool isPtrLike = isPointerLikeType(errTypePtr);

                    std::unique_ptr<Expr> rawInit;
                    if (!stmt->do_stmt.error_var.items.empty() && stmt->do_stmt.error_var.items[0].init) {
                        rawInit = std::move(stmt->do_stmt.error_var.items[0].init);
                    } else if (isPtrLike) {
                        rawInit = makeNullLiteral();
                    } else {
                        rawInit = createDefaultReturnExpr(errTypePtr);
                    }

                    auto desugaredBlock = std::make_unique<Stmt>();
                    desugaredBlock->kind = Stmt::Kind::Block;

                    // 1. T __err_slot_N_init = <rawInit>;
                    VarDecl initDecl;
                    initDecl.type = cloneType(errTypePtr);
                    VarDecl::Item initItem;
                    initItem.name = initVarName;
                    initItem.init = std::move(rawInit);
                    initDecl.items.push_back(std::move(initItem));

                    auto initDeclStmt = std::make_unique<Stmt>();
                    initDeclStmt->kind = Stmt::Kind::VarDecl;
                    initDeclStmt->var_decl = std::move(initDecl);
                    desugaredBlock->block.stmts.push_back(std::move(initDeclStmt));

                    // 2. T __err_slot_N = __err_slot_N_init;
                    VarDecl slotDecl;
                    slotDecl.type = cloneType(errTypePtr);
                    VarDecl::Item slotItem;
                    slotItem.name = slotName;
                    slotItem.init = makeIdentExpr(initVarName);
                    slotDecl.items.push_back(std::move(slotItem));

                    auto slotDeclStmt = std::make_unique<Stmt>();
                    slotDeclStmt->kind = Stmt::Kind::VarDecl;
                    slotDeclStmt->var_decl = std::move(slotDecl);
                    desugaredBlock->block.stmts.push_back(std::move(slotDeclStmt));

                    // 3. Kumpulkan statement body + sisipkan cek langsung di bawah tiap call throwing
                    std::vector<std::unique_ptr<Stmt>> bodyList;
                    if (stmt->do_stmt.body) {
                        if (stmt->do_stmt.body->kind == Stmt::Kind::Block) {
                            for (auto& child : stmt->do_stmt.body->block.stmts) {
                                bodyList.push_back(std::move(child));
                            }
                        } else {
                            bodyList.push_back(std::move(stmt->do_stmt.body));
                        }
                    }

                    for (auto& s : bodyList) {
                        bool hasThrowCall = tryHandleStmtInDo(s.get(), slotName);
                        if (!hasThrowCall) {
                            lowerStmt(s.get());
                        }

                        desugaredBlock->block.stmts.push_back(std::move(s));

                        if (hasThrowCall) {
                            auto ifStmt = std::make_unique<Stmt>();
                            ifStmt->kind = Stmt::Kind::If;

                            auto condExpr = std::make_unique<Expr>();
                            condExpr->kind = Expr::Kind::Binary;
                            condExpr->binary.op = BinaryOp::Ne;
                            condExpr->binary.lhs = makeIdentExpr(slotName);
                            condExpr->binary.rhs = isPtrLike ? makeNullLiteral() : makeIdentExpr(initVarName);
                            ifStmt->if_stmt.cond = std::move(condExpr);

                            auto jumpStmt = std::make_unique<Stmt>();
                            jumpStmt->kind = Stmt::Kind::Jump;
                            jumpStmt->jump_target = catchLabel;

                            auto thenBlock = std::make_unique<Stmt>();
                            thenBlock->kind = Stmt::Kind::Block;
                            thenBlock->block.stmts.push_back(std::move(jumpStmt));
                            ifStmt->if_stmt.then_block = std::move(thenBlock);

                            desugaredBlock->block.stmts.push_back(std::move(ifStmt));
                        }
                    }

                    // 4. Sukses semua -> lompat ke akhir
                    auto jumpEnd = std::make_unique<Stmt>();
                    jumpEnd->kind = Stmt::Kind::Jump;
                    jumpEnd->jump_target = endLabel;
                    desugaredBlock->block.stmts.push_back(std::move(jumpEnd));

                    // 5. Label catch
                    auto lblCatch = std::make_unique<Stmt>();
                    lblCatch->kind = Stmt::Kind::Label;
                    lblCatch->label = catchLabel;
                    desugaredBlock->block.stmts.push_back(std::move(lblCatch));

                    if (!stmt->do_stmt.error_var.items.empty()) {
                        stmt->do_stmt.error_var.items[0].init = makeIdentExpr(slotName);
                        auto errorVarStmt = std::make_unique<Stmt>();
                        errorVarStmt->kind = Stmt::Kind::VarDecl;
                        errorVarStmt->var_decl = std::move(stmt->do_stmt.error_var);
                        desugaredBlock->block.stmts.push_back(std::move(errorVarStmt));
                    }

                    if (stmt->do_stmt.catch_body) {
                        lowerStmt(stmt->do_stmt.catch_body.get());
                        desugaredBlock->block.stmts.push_back(std::move(stmt->do_stmt.catch_body));
                    }

                    // 6. Label akhir
                    auto lblEnd = std::make_unique<Stmt>();
                    lblEnd->kind = Stmt::Kind::Label;
                    lblEnd->label = endLabel;
                    desugaredBlock->block.stmts.push_back(std::move(lblEnd));

                    *stmt = std::move(*desugaredBlock);
                }
                break;
            }


            case Stmt::Kind::SwitchCase: {
                for (auto& c : stmt->switch_stmt.cases) {
                    if (c.body) lowerStmt(c.body.get());
                }
                break;
            }

            case Stmt::Kind::Unsafe: {
                if (stmt->unsafe_stmt.stmt) lowerStmt(stmt->unsafe_stmt.stmt.get());
                break;
            }

            // --- Perbaikan ThrowStmt ---
                        // --- Perbaikan ThrowStmt ---
            case Stmt::Kind::ThrowStmt: {
                // if (__throwed == null) { *__throwed = <expr>; }
                // return <default>;
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                auto lhsDeref = std::make_unique<Expr>();
                lhsDeref->kind = Expr::Kind::Unary;
                lhsDeref->unary.op = UnaryOp::Deref; // *
                lhsDeref->unary.expr = makeIdentExpr("__throwed");

                assignExpr->assign.lhs = std::move(lhsDeref);
                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                auto storeStmt = std::make_unique<Stmt>();
                storeStmt->kind = Stmt::Kind::ExprStmt;
                storeStmt->expr_stmt = std::move(assignExpr);

                auto thenBlock = std::make_unique<Stmt>();
                thenBlock->kind = Stmt::Kind::Block;
                thenBlock->block.stmts.push_back(std::move(storeStmt));

                auto condExpr = std::make_unique<Expr>();
                condExpr->kind = Expr::Kind::Binary;
                condExpr->binary.op = BinaryOp::Ne; // __throwed == null
                condExpr->binary.lhs = makeIdentExpr("__throwed");
                condExpr->binary.rhs = makeNullLiteral();

                auto ifStmt = std::make_unique<Stmt>();
                ifStmt->kind = Stmt::Kind::If;
                ifStmt->if_stmt.cond = std::move(condExpr);
                ifStmt->if_stmt.then_block = std::move(thenBlock);

                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = createDefaultReturnExpr(current_return_type);

                stmt->kind = Stmt::Kind::Block;
                stmt->block.stmts.clear();
                stmt->block.stmts.push_back(std::move(ifStmt));
                stmt->block.stmts.push_back(std::move(returnStmt));

                break;
            }


            default:
                break;
        }
    }
};

void lowerAST(FileProgram* program) {
    LoweringEngine engine;
    engine.lowerProgram(program);
}
