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
        if (!func->is_throws || !func->throws_type) {
            if (func->body) {
                lowerStmt(func->body.get());
            }
            return;
        }

        current_return_type = func->return_types.get();

        // 1. Buat parameter tambahan out-parameter (pointer)
        Function::Param errorParam;
        errorParam.type = std::move(func->throws_type); 
        
        TypeModifier mod;
        mod.kind = TypeModifier::Kind::Pointer;
        errorParam.type->modifiers.push_back(std::move(mod));

        errorParam.name = "__throwed";
        errorParam.init = nullptr;

        func->params.push_back(std::move(errorParam));

        // 2. Transformasi body fungsi
        if (func->body) {
            lowerStmt(func->body.get());
        }

        func->is_throws = false;
        func->throws_type = nullptr;
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
                if (stmt->if_stmt.then_block) lowerStmt(stmt->if_stmt.then_block.get());
                for (auto& elif : stmt->if_stmt.elifs) lowerStmt(elif.block.get());
                if (stmt->if_stmt.else_block) lowerStmt(stmt->if_stmt.else_block.get());
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
                    // Cari ekspresi pemanggilan fungsi di dalam body (misal: divide(0, 1))
                    // Asumsi body membungkus ExprStmt yang berisi Postfix Call
                    Expr* callExpr = nullptr;
                    if (stmt->do_stmt.body->kind == Stmt::Kind::ExprStmt && 
                        stmt->do_stmt.body->expr_stmt && 
                        stmt->do_stmt.body->expr_stmt->kind == Expr::Kind::Postfix) {
                        
                        auto& postfix = stmt->do_stmt.body->expr_stmt->postfix;
                        // Cek apakah base adalah fungsi yang ada di throwing_functions
                        if (postfix.base && postfix.base->kind == Expr::Kind::Ident && 
                            postfix.base->ident && !postfix.base->ident->parts.empty()) {
                            
                            std::string funcName = postfix.base->ident->parts.back();
                            if (throwing_functions.count(funcName)) {
                                // Temukan operasi Call di postfix.ops
                                for (auto& op : postfix.ops) {
                                    if (op.kind == Expr::PostfixOp::Kind::Call) {
                                        // Buat ekspresi referensi ke variabel error pointer: &__err_ptr
                                        auto refExpr = std::make_unique<Expr>();
                                        refExpr->kind = Expr::Kind::Unary;
                                        refExpr->unary.op = UnaryOp::Ref;

                                        auto identExpr = std::make_unique<Expr>();
                                        identExpr->kind = Expr::Kind::Ident;
                                        identExpr->ident = std::make_unique<QualifiedName>();
                                        identExpr->ident->parts.push_back("__err_ptr");

                                        refExpr->unary.expr = std::move(identExpr);

                                        // Masukkan ke argumen fungsi
                                        op.args.push_back(std::move(refExpr));
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Buat blok hasil desugar: { ptr __err_ptr = null; [body_asli]; if (__err_ptr != null) { int e = *__err_ptr; catch_body; } }
                    auto desugaredBlock = std::make_unique<Stmt>();
                    desugaredBlock->kind = Stmt::Kind::Block;

                    // 1. Deklarasi ptr __err_ptr = null;
                    auto varDeclStmt = std::make_unique<Stmt>();
                    varDeclStmt->kind = Stmt::Kind::VarDecl;
                    VarDecl vdecl;
                    // Tentukan tipe pointer (bisa disesuaikan, misal generik ptr atau tipe pointer error)
                    auto ptrType = std::make_unique<Type>();
                    auto baseName = std::make_unique<QualifiedName>();
                    baseName->parts.push_back("ptr");
                    ptrType->base = std::move(baseName);
                    vdecl.type = std::move(ptrType);

                    VarDecl::Item item;
                    item.name = "__err_ptr";
                    auto nullLit = std::make_unique<Expr>();
                    nullLit->kind = Expr::Kind::Literal;
                    nullLit->literal = std::make_unique<Literal>();
                    nullLit->literal->kind = Literal::Kind::Null;
                    nullLit->literal->value = "null";
                    item.init = std::move(nullLit);
                    vdecl.items.push_back(std::move(item));
                    varDeclStmt->var_decl = std::move(vdecl);

                    desugaredBlock->block.stmts.push_back(std::move(varDeclStmt));

                    // 2. Masukkan body asli (pemanggilan fungsi yang argumennya sudah ditambah &__err_ptr)
                    desugaredBlock->block.stmts.push_back(std::move(stmt->do_stmt.body));

                    // 3. Buat If statement: if (__err_ptr != null)
                    auto ifStmt = std::make_unique<Stmt>();
                    ifStmt->kind = Stmt::Kind::If;

                    auto condExpr = std::make_unique<Expr>();
                    condExpr->kind = Expr::Kind::Binary;
                    condExpr->binary.op = BinaryOp::Ne;

                    auto lhs = std::make_unique<Expr>();
                    lhs->kind = Expr::Kind::Ident;
                    lhs->ident = std::make_unique<QualifiedName>();
                    lhs->ident->parts.push_back("__err_ptr");

                    auto rhs = std::make_unique<Expr>();
                    rhs->kind = Expr::Kind::Literal;
                    rhs->literal = std::make_unique<Literal>();
                    rhs->literal->kind = Literal::Kind::Null;
                    rhs->literal->value = "0";

                    condExpr->binary.lhs = std::move(lhs);
                    condExpr->binary.rhs = std::move(rhs);
                    ifStmt->if_stmt.cond = std::move(condExpr);

                    // Buat isi block untuk then_block (deklarasi variabel error 'e = *__err_ptr' + catch_body)
                    auto catchBlock = std::make_unique<Stmt>();
                    catchBlock->kind = Stmt::Kind::Block;

                    // Buat dereference: *__err_ptr
                    auto derefExpr = std::make_unique<Expr>();
                    derefExpr->kind = Expr::Kind::Unary;
                    derefExpr->unary.op = UnaryOp::Deref;
                    
                    auto idExpr = std::make_unique<Expr>();
                    idExpr->kind = Expr::Kind::Ident;
                    idExpr->ident = std::make_unique<QualifiedName>();
                    idExpr->ident->parts.push_back("__err_ptr");
                    derefExpr->unary.expr = std::move(idExpr);

                    // Set init pada error_var (misal: int e = *__err_ptr)
                    if (!stmt->do_stmt.error_var.items.empty()) {
                        stmt->do_stmt.error_var.items[0].init = std::move(derefExpr);
                        
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

                    // Ganti stmt saat ini menjadi blok hasil desugar
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
                // throw 7;  -->  *__throwed = 7; return 0;
                // Kita gunakan UnaryOp::Deref pada __throwed agar menyimpannya ke memori tujuan
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                auto lhsDeref = std::make_unique<Expr>();
                lhsDeref->kind = Expr::Kind::Unary;
                lhsDeref->unary.op = UnaryOp::Deref;
                
                auto lhsIdent = std::make_unique<Expr>();
                lhsIdent->kind = Expr::Kind::Ident;
                lhsIdent->ident = std::make_unique<QualifiedName>();
                lhsIdent->ident->parts.push_back("__throwed");
                lhsDeref->unary.expr = std::move(lhsIdent);

                assignExpr->assign.lhs = std::move(lhsDeref);
                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                auto exprStmt = std::make_unique<Stmt>();
                exprStmt->kind = Stmt::Kind::ExprStmt;
                exprStmt->expr_stmt = std::move(assignExpr);

                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = createDefaultReturnExpr(current_return_type);

                stmt->kind = Stmt::Kind::Block;
                stmt->block.stmts.clear();
                stmt->block.stmts.push_back(std::move(exprStmt));
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
