#include <memory>
#include <vector>
#include <stdexcept>

class LoweringEngine {
public:
    void lowerProgram(FileProgram* program) {
        if (!program) return;

        for (auto& node : program->decls) {
            if (auto func = dynamic_cast<Function*>(node.get())) {
                lowerFunction(func);
            }
        }
    }

private:
    void lowerFunction(Function* func) {
        // Jika fungsi tidak memiliki klausa throws (is_throws == false atau throws_type kosong), lewati.
        if (!func->is_throws || !func->throws_type) {
            return;
        }

        // 1. Buat parameter tambahan untuk menampung error (out-parameter)
        // Contoh: Tipe error diambil dari func->throws_type, nama parameter tersembunyi misal "__throwed"
        Function::Param errorParam;
        errorParam.type = std::move(func->throws_type); // Salin tipe data error
        auto mod = std::make_unique<TypeModifier>();
        mod.kind = TypeModifier::Kind::Pointer;
        errorParam.type.modifiers.push(std::move(mod));
        errorParam.name = "__throwed";
        errorParam.init = nullptr;

        // Masukkan parameter error ke dalam daftar parameter fungsi
        func->params.push_back(std::move(errorParam));

        // 2. Transformasi isi badan fungsi (body) untuk menangani statement 'throw expr;'
        if (func->body) {
            lowerStmt(func->body.get(), func->throws_type.get());
        }

        // 3. Ubah status fungsi agar tidak lagi dianggap 'throws' mentah, 
        // karena sudah berhasil didesugar menjadi parameter biasa di level lower.
        func->is_throws = false;
        func->throws_type = nullptr;
    }

    void lowerStmt(Stmt* stmt, Type* throwsType) {
        if (!stmt) return;

        switch (stmt->kind) {
            case Stmt::Kind::Block: {
                for (auto& s : stmt->block.stmts) {
                    lowerStmt(s.get(), throwsType);
                }
                break;
            }

            case Stmt::Kind::If: {
                if (stmt->if_stmt.then_block) {
                    lowerStmt(stmt->if_stmt.then_block.get(), throwsType);
                }
                for (auto& elif : stmt->if_stmt.elifs) {
                    lowerStmt(elif.block.get(), throwsType);
                }
                if (stmt->if_stmt.else_block) {
                    lowerStmt(stmt->if_stmt.else_block.get(), throwsType);
                }
                break;
            }

            case Stmt::Kind::While: {
                if (stmt->while_stmt.body) {
                    lowerStmt(stmt->while_stmt.body.get(), throwsType);
                }
                break;
            }

            case Stmt::Kind::DoStmt: {
                if (stmt->do_stmt.body) {
                    lowerStmt(stmt->do_stmt.body.get(), throwsType);
                }
                if (stmt->do_stmt.catch_body) {
                    lowerStmt(stmt->do_stmt.catch_body.get(), throwsType);
                }
                break;
            }

            case Stmt::Kind::SwitchCase: {
                for (auto& c : stmt->switch_stmt.cases) {
                    if (c.body) {
                        lowerStmt(c.body.get(), throwsType);
                    }
                }
                break;
            }

            case Stmt::Kind::Unsafe: {
                if (stmt->unsafe_stmt.stmt) {
                    lowerStmt(stmt->unsafe_stmt.stmt.get(), throwsType);
                }
                break;
            }

            // --- INI TRANSFORMASI UTAMA UNTUK 'throw expr;' ---
            case Stmt::Kind::ThrowStmt: {
                // Ubah Stmt::Kind::ThrowStmt menjadi Assignment + Return dini:
                // throw 45;  -->  __throwed = 45; return;

                // 1. Buat expression assignment: __throwed = <expr>
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                // Buat LHS: ident__throwed
                auto lhsIdent = std::make_unique<Expr>();
                lhsIdent->kind = Expr::Kind::Ident;
                lhsIdent->ident = std::make_unique<QualifiedName>();
                lhsIdent->ident->parts.push_back("__throwed");
                assignExpr->assign.lhs = std::move(lhsIdent);

                // Pindahkan expr dari throw_stmt ke RHS assign
                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                // 2. Bungkus assignment kedalam ExprStmt
                auto exprStmt = std::make_unique<Stmt>();
                exprStmt->kind = Stmt::Kind::ExprStmt;
                exprStmt->expr_stmt = std::move(assignExpr);

                // 3. Buat Stmt::Kind::Return kosong untuk early return
                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = nullptr;

                // 4. Ubah node stmt saat ini menjadi blok yang berisi (assignExpr, returnStmt)
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

// Fungsi pembantu untuk memanggil lowering engine dari luar
void lowerAST(FileProgram* program) {
    LoweringEngine engine;
    engine.lowerProgram(program);
}
