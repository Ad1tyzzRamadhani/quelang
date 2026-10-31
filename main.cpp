#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib> // Untuk system()

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

void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " <source_file.q> [-cro <output.cro>] [-o <output_binary>] [-l <additional_file/lib>]\n";
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }

        std::string source_file = argv[1];
        std::string cro_output_file = ""; // File IR perantara
        std::string binary_output_file = ""; // Output binary akhir (jika menggunakan -o)
        std::vector<std::string> linked_files; // Daftar file tambahan untuk -l

        // Parsing argumen tambahan
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-cro") {
                if (i + 1 < argc) {
                    cro_output_file = argv[++i];
                }
            } else if (arg == "-o") {
                if (i + 1 < argc) {
                    binary_output_file = argv[++i];
                }
            } else if (arg == "-l") {
                if (i + 1 < argc) {
                    linked_files.push_back(argv[++i]);
                }
            }
        }

        // Jika -o digunakan tapi -cro tidak diset, buat file .cro sementara (misal: a.cro)
        if (cro_output_file.empty()) {
            cro_output_file = (binary_output_file.empty()) ? "a.cro" : binary_output_file + ".cro";
        }

        // --- PROSES KOMPILASI KE IR (.cro) ---
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

        if (!validate || has_error) {
            std::cerr << "Compilation failed: Semantic errors detected.\n";
            return 1;
        }
        
        std::string outputcru = codegen(ast.get());
        
        std::ofstream outFile(cro_output_file);
        if (!outFile) {
            std::cerr << "CRITICAL EXCEPTION: Could not open output file '" << cro_output_file << "' for writing.\n";
            return 1;
        }
        outFile << outputcru;
        outFile.close();
        
        std::cout << "IR generation successful! Output written to " << cro_output_file << "\n";

        // --- PROSES LINKING / ASSEMBLING KE BINARY (Jika -o ditentukan) ---
        if (!binary_output_file.empty()) {
            // Kita ubah file .cro menjadi file assembly/object lalu panggil gcc/ld di bawah hood
            std::string temp_asm = cro_output_file + ".s";
            
            // Simpan outputcru sebagai file assembly (.s) sementara untuk dirakit oleh gcc
            std::ofstream asmFile(temp_asm);
            asmFile << outputcru;
            asmFile.close();

            // Konstruksi perintah gcc/ld otomatis
            // Contoh: gcc a.cro.s file_tambahan.o -o binary_name
            std::string compile_cmd = "./cro" + temp_asm;
            for (const auto& link_f : linked_files) {
                compile_cmd += " " + link_f;
            }
            compile_cmd += " -o " + binary_output_file;

            std::cout << "Running linker/assembler: " << compile_cmd << "\n";
            int link_result = std::system(compile_cmd.c_str());

            // Bersihkan file assembly sementara
            std::string rm_cmd = "rm " + temp_asm;
            std::system(rm_cmd.c_str());

            if (link_result != 0) {
                std::cerr << "CRITICAL EXCEPTION: Linker failed to produce binary executable.\n";
                return 1;
            }

            std::cout << "Binary compilation successful! Executable created: " << binary_output_file << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL EXCEPTION: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN CRASH / EXCEPTION (Possible Null Pointer or Segfault)\n";
        return 1;
    }
}
