#include <memory>
#include <vector>
#include <unordered_set>
#include <stdexcept>

class LoweringEngine {
private:
    Type* current_return_type = nullptr;
    std::unordered_set<std::string> throwing_functions; // Menyimpan daftar nama fungsi yang melempar error

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
                    lowerStmt(stmt->if_stmt.then_block.get()); // Pastikan ini memanggil lowerStmt secara rekursif!
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

        Type* errTypePtr = stmt->do_stmt.error_var.type.get();
        bool isPtrLike = isPointerLikeType(errTypePtr);

        // Ambil expr init aslinya sekali saja (di-move, tidak perlu di-clone!)
        std::unique_ptr<Expr> rawInit;
        if (!stmt->do_stmt.error_var.items.empty() && stmt->do_stmt.error_var.items[0].init) {
            rawInit = std::move(stmt->do_stmt.error_var.items[0].init);
        } else if (isPtrLike) {
            rawInit = makeNullLiteral();
        } else {
            rawInit = createDefaultReturnExpr(errTypePtr);
        }

        // Inject &__err_slot_N ke pemanggilan throwing_functions di dalam body
        if (stmt->do_stmt.body &&
            stmt->do_stmt.body->kind == Stmt::Kind::ExprStmt &&
            stmt->do_stmt.body->expr_stmt &&
            stmt->do_stmt.body->expr_stmt->kind == Expr::Kind::Postfix) {

            auto& postfix = stmt->do_stmt.body->expr_stmt->postfix;
            if (postfix.base && postfix.base->kind == Expr::Kind::Ident &&
                postfix.base->ident && !postfix.base->ident->parts.empty()) {

                std::string funcName = postfix.base->ident->parts.back();
                if (throwing_functions.count(funcName)) {
                    for (auto& op : postfix.ops) {
                        if (op.kind == Expr::PostfixOp::Kind::Call) {
                            op.args.push_back(makeRefExpr(slotName));
                            break;
                        }
                    }
                }
            }
        }

        auto desugaredBlock = std::make_unique<Stmt>();
        desugaredBlock->kind = Stmt::Kind::Block;

        // 1. Simpan nilai awal ke variabel lokal: T __err_slot_0_init = <rawInit>;
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

        // 2. Wadah error ikut diinisialisasi dari __err_slot_0_init: T __err_slot_0 = __err_slot_0_init;
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

        // 3. Eksekusi body asli (yang argumennya sudah disuntik &__err_slot_0)
        desugaredBlock->block.stmts.push_back(std::move(stmt->do_stmt.body));

        // 4. Cek kondisi:
        //    - pointer/ref: __err_slot_0 != null
        //    - stack/value: __err_slot_0 != __err_slot_0_init
        auto ifStmt = std::make_unique<Stmt>();
        ifStmt->kind = Stmt::Kind::If;

        auto condExpr = std::make_unique<Expr>();
        condExpr->kind = Expr::Kind::Binary;
        condExpr->binary.op = BinaryOp::Ne;
        condExpr->binary.lhs = makeIdentExpr(slotName);
        condExpr->binary.rhs = isPtrLike ? makeNullLiteral() : makeIdentExpr(initVarName);
        ifStmt->if_stmt.cond = std::move(condExpr);

        // 5. Blok catch: T name = __err_slot_0; <catch_body>
        auto catchBlock = std::make_unique<Stmt>();
        catchBlock->kind = Stmt::Kind::Block;

        if (!stmt->do_stmt.error_var.items.empty()) {
            stmt->do_stmt.error_var.items[0].init = makeIdentExpr(slotName);

            auto errorVarStmt = std::make_unique<Stmt>();
            errorVarStmt->kind = Stmt::Kind::VarDecl;
            errorVarStmt->var_decl = std::move(stmt->do_stmt.error_var);
            catchBlock->block.stmts.push_back(std::move(errorVarStmt));
        }

        if (stmt->do_stmt.catch_body) {
            lowerStmt(stmt->do_stmt.catch_body.get());
            catchBlock->block.stmts.push_back(std::move(stmt->do_stmt.catch_body));
        }

        ifStmt->if_stmt.then_block = std::move(catchBlock);
        desugaredBlock->block.stmts.push_back(std::move(ifStmt));

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

            // --- 1. Perbaikan ThrowStmt ---
            case Stmt::Kind::ThrowStmt: {
                // *__throwed = <expr>; return 0;
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                auto lhsDeref = std::make_unique<Expr>();
                lhsDeref->kind = Expr::Kind::Unary;
                lhsDeref->unary.op = UnaryOp::Deref; // *
                
                auto lhsIdent = std::make_unique<Expr>();
                lhsIdent->kind = Expr::Kind::Ident;
                lhsIdent->ident = std::make_unique<QualifiedName>();
                lhsIdent->ident->parts.push_back("__throwed");
                lhsDeref->unary.expr = std::move(lhsIdent);

                assignExpr->assign.lhs = std::move(lhsDeref);
                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                auto storeStmt = std::make_unique<Stmt>();
                storeStmt->kind = Stmt::Kind::ExprStmt;
                storeStmt->expr_stmt = std::move(assignExpr);

                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = createDefaultReturnExpr(current_return_type);

                stmt->kind = Stmt::Kind::Block;
                stmt->block.stmts.clear();
                stmt->block.stmts.push_back(std::move(storeStmt));
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
