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
    Type* current_return_type = nullptr;
    void lowerFunction(Function* func) {
        // Jika fungsi tidak memiliki klausa throws (is_throws == false atau throws_type kosong), lewati.
        if (!func->is_throws || !func->throws_type) {
            return;
        }

        // 1. Buat parameter tambahan untuk menampung error (out-parameter)
        current_return_type = func->return_types.get();
        // Contoh: Tipe error diambil dari func->throws_type, nama parameter tersembunyi misal "__throwed"
        Function::Param errorParam;
        errorParam.type = std::move(func->throws_type); // Salin tipe data error
        TypeModifier mod;
        mod.kind = TypeModifier::Kind::Pointer;
        errorParam.type->modifiers.push_back(std::move(mod));
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
        current_return_type = nullptr;
    }

    std::unique_ptr<Expr> createDefaultReturnExpr(Type* type) {
        if (!type || !type->base || type->base->parts.empty()) {
            return nullptr; // Jika void / tidak ada return type
        }

        const std::string& typeName = type->base->parts.back();

        // Cek apakah tipe primitif Integer / Number
        if (typeName == "int" || typeName == "i32" || typeName == "i64" || 
            typeName == "u32" || typeName == "u64" || typeName == "byte" || typeName == "char") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Number;
            expr->literal->value = "0";
            return expr;
        }
        
        // Cek apakah tipe primitif Float / Double
        if (typeName == "float" || typeName == "double" || typeName == "f32" || typeName == "f64") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::Float;
            expr->literal->value = "0.0";
            return expr;
        }

        // Cek apakah tipe Boolean
        if (typeName == "bool") {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Literal;
            expr->literal = std::make_unique<Literal>();
            expr->literal->kind = Literal::Kind::False;
            expr->literal->value = "false";
            return expr;
        }

        // Jika bukan primitif, berarti berupa Objek / Struct -> Lakukan Zero Init (StructInit kosong)
                // Jika bukan primitif, berarti berupa Objek / Struct -> Lakukan Zero Init (StructInit kosong)
        auto structExpr = std::make_unique<Expr>();
        structExpr->kind = Expr::Kind::StructInit;
        
        // Buat salinan Type dengan aman (Deep Copy manual untuk menghindari copy constructor unique_ptr)
        auto copiedType = std::make_unique<Type>();
        if (type) {
            copiedType->file = type->file;
            copiedType->line_start = type->line_start;
            copiedType->column_start = type->column_start;
            copiedType->is_array = type->is_array;
            copiedType->unsized_array = type->unsized_array;
            copiedType->qualifiers = type->qualifiers;
            
            // Deep copy base (QualifiedName) jika ada
            if (type->base) {
                auto newBase = std::make_unique<QualifiedName>();
                newBase->parts = type->base->parts;
                copiedType->base = std::move(newBase);
            }
            
            // Salin modifiers jika diperlukan
            for (const auto& mod : type->modifiers) {
                TypeModifier m;
                m.kind = mod.kind;
                // salin bagian lain dari mod jika ada...
                copiedType->modifiers.push_back(m);
            }
        }

        structExpr->struct_init.type_name = std::move(copiedType);
        structExpr->struct_init.fields.clear(); // Kosongkan field untuk zero-init (misal: Point{})
        return structExpr;
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
                // 1. Buat expression assignment: *__throwed = <expr_throw>
                // (Catatan: Menggunakan dereference atau langsung assign ke parameter pointer)
                auto assignExpr = std::make_unique<Expr>();
                assignExpr->kind = Expr::Kind::Assign;
                assignExpr->assign.op = AssignOp::Assign;

                auto lhsIdent = std::make_unique<Expr>();
                lhsIdent->kind = Expr::Kind::Ident;
                lhsIdent->ident = std::make_unique<QualifiedName>();
                lhsIdent->ident->parts.push_back("__throwed");
                assignExpr->assign.lhs = std::move(lhsIdent);

                assignExpr->assign.rhs = std::move(stmt->throw_stmt.expr);

                auto exprStmt = std::make_unique<Stmt>();
                exprStmt->kind = Stmt::Kind::ExprStmt;
                exprStmt->expr_stmt = std::move(assignExpr);

                // 2. Buat Stmt::Kind::Return dengan nilai default yang sesuai tipe fungsinya
                auto returnStmt = std::make_unique<Stmt>();
                returnStmt->kind = Stmt::Kind::Return;
                returnStmt->return_values = createDefaultReturnExpr(current_return_type);

                // 3. Ubah node stmt saat ini menjadi blok (assignExpr, returnStmt)
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
