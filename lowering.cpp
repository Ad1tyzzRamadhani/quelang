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

    // --- Tambah di private member LoweringEngine ---
int label_counter = 0;

std::string nextCatchLabel() {
    return "__catch_" + std::to_string(label_counter);
}

std::string nextEndLabel() {
    return "__do_end_" + std::to_string(label_counter++);
}

bool containsThrowingCall(const Expr* expr) const {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::Postfix: {
            if (containsThrowingCall(expr->postfix.base.get())) return true;
            if (expr->postfix.base &&
                expr->postfix.base->kind == Expr::Kind::Ident &&
                expr->postfix.base->ident &&
                !expr->postfix.base->ident->parts.empty()) {
                if (throwing_functions.count(expr->postfix.base->ident->parts.back())) {
                    for (const auto& op : expr->postfix.ops) {
                        if (op.kind == Expr::PostfixOp::Kind::Call) return true;
                    }
                }
            }
            for (const auto& op : expr->postfix.ops) {
                for (const auto& arg : op.args) {
                    if (containsThrowingCall(arg.get())) return true;
                }
                if (op.index && containsThrowingCall(op.index.get())) return true;
            }
            return false;
        }
        case Expr::Kind::Unary:
            return containsThrowingCall(expr->unary.expr.get());
        case Expr::Kind::Binary:
            return containsThrowingCall(expr->binary.lhs.get()) ||
                   containsThrowingCall(expr->binary.rhs.get());
        case Expr::Kind::Assign:
            return containsThrowingCall(expr->assign.lhs.get()) ||
                   containsThrowingCall(expr->assign.rhs.get());
        case Expr::Kind::Ternary:
            return containsThrowingCall(expr->ternary.cond.get()) ||
                   containsThrowingCall(expr->ternary.then_expr.get()) ||
                   containsThrowingCall(expr->ternary.else_expr.get());
        case Expr::Kind::Cast:
            return containsThrowingCall(expr->cast.base.get());
        default:
            return false;
    }
}

bool stmtHasThrowingCall(const Stmt* s) const {
    if (!s) return false;
    switch (s->kind) {
        case Stmt::Kind::ExprStmt:
            return containsThrowingCall(s->expr_stmt.get());
        case Stmt::Kind::VarDecl:
            for (const auto& it : s->var_decl.items) {
                if (containsThrowingCall(it.init.get())) return true;
                for (const auto& a : it.args) {
                    if (containsThrowingCall(a.get())) return true;
                }
            }
            return false;
        case Stmt::Kind::Return:
            return containsThrowingCall(s->return_values.get());
        default:
            return false;
    }
}

std::unique_ptr<Stmt> makeThrowCheckJumpStmt(
    const std::string& slotName,
    const std::string& initVarName,
    bool isPtrLike,
    const std::string& catchLabel
) const {
    auto ifStmt = std::make_unique<Stmt>();
    ifStmt->kind = Stmt::Kind::If;

    auto condExpr = std::make_unique<Expr>();
    condExpr->kind = Expr::Kind::Binary;
    condExpr->binary.op = BinaryOp::Ne;
    condExpr->binary.lhs = makeIdentExpr(slotName);
    condExpr->binary.rhs = isPtrLike ? makeNullLiteral() : makeIdentExpr(initVarName);
    ifStmt->if_stmt.cond = std::move(condExpr);

    auto thenBlock = std::make_unique<Stmt>();
    thenBlock->kind = Stmt::Kind::Block;
    thenBlock->block.stmts.push_back(makeJumpStmt(catchLabel));
    ifStmt->if_stmt.then_block = std::move(thenBlock);

    return ifStmt;
}

void appendLoweredDoBodyStmts(
    Stmt* bodyStmt,
    const std::string& slotName,
    const std::string& initVarName,
    bool isPtrLike,
    const std::string& catchLabel,
    std::vector<std::unique_ptr<Stmt>>& outStmts
) {
    if (!bodyStmt) return;

    if (bodyStmt->kind == Stmt::Kind::Block) {
        for (auto& child : bodyStmt->block.stmts) {
            appendLoweredDoBodyStmts(child.get(), slotName, initVarName, isPtrLike, catchLabel, outStmts);
        }
        return;
    }

    bool hasThrow = stmtHasThrowingCall(bodyStmt);
    injectErrSlotInStmt(bodyStmt, slotName);

    auto movedStmt = std::make_unique<Stmt>();
    *movedStmt = std::move(*bodyStmt);
    outStmts.push_back(std::move(movedStmt));

    if (hasThrow) {
        outStmts.push_back(makeThrowCheckJumpStmt(slotName, initVarName, isPtrLike, catchLabel));
    }
}


std::unique_ptr<Stmt> makeLabelStmt(const std::string& lbl) const {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Label;
    s->label = lbl;
    return s;
}

std::unique_ptr<Stmt> makeJumpStmt(const std::string& target) const {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Jump;
    s->jump_target = target;
    return s;
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
            }
        }

        // Pass 2: Lowering deklarasi (fungsi, dll)
        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                lowerFunction(func);
            }
        }
    }

private:
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
            case Stmt::Kind::DoStmt: {
    if (stmt->do_stmt.body) {
        lowerStmt(stmt->do_stmt.body.get());
    }

    if (stmt->do_stmt.iscatch) {
        std::string slotName = nextErrVarName();       // misal: __err_slot_0
        std::string initVarName = slotName + "_init";  // misal: __err_slot_0_init
        std::string catchLabel = nextCatchLabel();     // misal: __catch_0
        std::string endLabel = nextEndLabel();         // misal: __do_end_0

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

        // 1. T __err_slot_0_init = <rawInit>;
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

        // 2. T __err_slot_0 = __err_slot_0_init;
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

        // 3. Eksekusi statement di body; setiap throwing call langsung diikuti cek + jump __catch_N
        if (stmt->do_stmt.body) {
            appendLoweredDoBodyStmts(
                stmt->do_stmt.body.get(),
                slotName,
                initVarName,
                isPtrLike,
                catchLabel,
                desugaredBlock->block.stmts
            );
        }

        // 4. Jika tidak ada error hingga akhir body, lompat melewati blok catch
        desugaredBlock->block.stmts.push_back(makeJumpStmt(endLabel));

        // 5. Label catch: deklarasi variabel error + catch_body
        desugaredBlock->block.stmts.push_back(makeLabelStmt(catchLabel));

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

        // 6. Label akhir do...catch
        desugaredBlock->block.stmts.push_back(makeLabelStmt(endLabel));

        *stmt = std::move(*desugaredBlock);
    }
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
