#include "directivelinker.cpp"
#include "lexer.cpp"
#include "ast.hpp"
#include "parser.cpp"
#include "parser_literal.cpp"
#include "parser_type.cpp"
#include "parser_expr.cpp"
#include "parser_stmt.cpp"
#include "parser_toplevel.cpp"
#include "semantic_symbol.cpp"
#include "semantic.hpp"
#include "semantic.cpp"
#include "codegen.cpp"

int main(int argc, char* argv[]) {
    try {
        // Validasi argumen minimal (nama program + file input)
        if (argc < 2) {
            std::cerr << "Usage: qc <source_file.q> [-cro <output_file.cro>]\n";
            return 1;
        }

        std::string source_file = argv[1];
        std::string output_file = "a.cro"; // Default output

        // Parsing argumen tambahan untuk -cro
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-cro") {
                if (i + 1 < argc) {
                    output_file = argv[i + 1];
                    i++; // Lewati nama file output di iterasi berikutnya
                } else {
                    // Jika ada flag -cro tapi tidak ada nama file di depannya, 
                    // kita bisa biarkan default atau berikan nama file standar jika diinginkan.
                    // Di sini kita biarkan tetap 'a.cro' atau tangani sesuai kebutuhan.
                }
            }
        }

        std::string rawprogram = preprocess(source_file);
        Lexer lex(rawprogram);
        std::vector<Token> token = lex.tokenize();
        ParseState parser{token};
        SemanticAnalyzer semantic;
        
        auto ast = parser.parseProgram();
        bool validate = semantic.analyze(*ast);
        auto diagnostics = semantic.diagnostics();
        
        bool has_error = false;
        for (auto diagnostic : diagnostics) {
            if (diagnostic.severity == SemanticSeverity::Error) {
                has_error = true;
            }
            if (diagnostic.is_warning) {
                std::cerr << diagnostic.file + " : " + diagnostic.message + "\n";
                continue;
            }
            std::cerr << diagnostic.file + " : " << diagnostic.line << ":" << diagnostic.column << ": " + diagnostic.message + "\n";
        }

        // Jika semantic gagal atau ada error pada diagnostik, gagalkan proses dan jangan buat file
        if (!validate || has_error) {
            std::cerr << "Compilation failed: Semantic errors detected.\n";
            return 1;
        }
        
        // Jika lolos, lakukan code generation dan tulis file output
        std::string outputcru = codegen(ast.get());
        
        std::ofstream outFile(output_file);
        if (!outFile) {
            std::cerr << "CRITICAL EXCEPTION: Could not open output file '" << output_file << "' for writing.\n";
            return 1;
        }
        outFile << outputcru;
        outFile.close();
        
        std::cout << "Compilation successful! Output written to " << output_file << "\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL EXCEPTION: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN CRASH / EXCEPTION (Possible Null Pointer or Segfault)\n";
        return 1;
    }
}
