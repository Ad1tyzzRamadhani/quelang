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
    int label_counter = 0;

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

        // --- Tambah di private LoweringEngine ---
    void lowerExpr(Expr* expr) {
        if (!expr) return;

        switch (expr->kind) {
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
                break;
            }

            case Stmt::Kind::VarDecl: {
                for (auto& it : stmt->var_decl.items) {
                    lowerExpr(it.init.get());
                    for (auto& a : it.args) {
                        lowerExpr(a.get());
                    }
                }
                break;
            }

            case Stmt::Kind::Return: {
                lowerExpr(stmt->return_values.get());
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
                        bool hasThrowCall = false;
                        Expr* targetExpr = nullptr;
                        if (s->kind == Stmt::Kind::ExprStmt) targetExpr = s->expr_stmt.get();
                        else if (s->kind == Stmt::Kind::VarDecl && !s->var_decl.items.empty()) targetExpr = s->var_decl.items[0].init.get();
                        else if (s->kind == Stmt::Kind::Return) targetExpr = s->return_values.get();

                        if (targetExpr && targetExpr->kind == Expr::Kind::Postfix &&
                            targetExpr->postfix.base && targetExpr->postfix.base->kind == Expr::Kind::Ident &&
                            targetExpr->postfix.base->ident && !targetExpr->postfix.base->ident->parts.empty()) {
                            std::string fname = targetExpr->postfix.base->ident->parts.back();
                            if (throwing_functions.count(fname)) {
                                for (auto& op : targetExpr->postfix.ops) {
                                    if (op.kind == Expr::PostfixOp::Kind::Call) {
                                        op.args.push_back(makeRefExpr(slotName));
                                        hasThrowCall = true;
                                        break;
                                    }
                                }
                            }
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
