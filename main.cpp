#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <stdexcept>

#include "directivelinker.cpp"
#include "lexer.cpp"
#include "ast.hpp"
#include "parser.cpp"
#include "parser_expr.cpp"
#include "parser_type.cpp"
#include "parser_stmt.cpp"
#include "parser_toplevel.cpp"
#include "semantic_symbol.cpp"
#include "semantic.hpp"
#include "semantic.cpp"
#include "lowering.cpp"
#include "codegen.cpp"

namespace fs = std::filesystem;

namespace {

bool hasEnding(const std::string& fullString, const std::string& ending) {
    if (fullString.length() < ending.length()) {
        return false;
    }
    return fullString.compare(fullString.length() - ending.length(), ending.length(), ending) == 0;
}

std::string stripExtension(const std::string& pathStr) {
    fs::path p(pathStr);
    return (p.parent_path() / p.stem()).string();
}

std::string shellQuote(const std::string& s) {
    std::string quoted = "'";
    for (char c : s) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

int runCommand(const std::string& cmd, bool debug, const std::string& stageName) {
    if (debug) {
        std::cerr << "[DEBUG][" << stageName << "] exec: " << cmd << "\n";
    }
    int status = std::system(cmd.c_str());
    if (status != 0) {
        std::cerr << "[ERROR][" << stageName << "] Command failed with exit code " << status << "\n";
        if (!debug) {
            std::cerr << "  Command: " << cmd << "\n";
        }
    }
    return status;
}

void printUsage(const char* progName) {
    std::cerr << "Usage:\n"
              << "  " << progName << " <file.q> [more.q ...] [-cru <output.cro>] [-c [output.o]] [-o <output_binary>] [-l <file.q|file.o>] [-warning] [--debug]\n"
              << "\n"
              << "Flags:\n"
              << "  -cru <file.cro>   Write generated CRU IR to <file.cro> (alias: -cr)\n"
              << "  -c [file.o]       Compile to object file (.o) only, not an executable\n"
              << "  -o <file>         Compile and link into an executable via ./cru\n"
              << "  -l <file.q|.o>    Add extra source (.q) or precompiled object (.o)\n"
              << "  -warning          Show semantic warnings\n"
              << "  --debug, -v       Show verbose debug logs and keep temp IR files\n";
}

bool compileSingleQToCro(const std::string& qFile,
                         const std::string& croOutputFile,
                         bool showWarnings,
                         bool debug) {
    if (debug) {
        std::cerr << "[DEBUG][FRONTEND] Preprocessing: " << qFile << "\n";
    }
    std::string rawprogram = preprocess(qFile);

    Lexer lex(rawprogram);
    std::vector<Token> tokens = lex.tokenize();

    ParseState parser{tokens};
    auto ast = parser.parseProgram();
    if (!ast) {
        std::cerr << "[ERROR][PARSER] Failed to produce AST for '" << qFile << "'.\n";
        return false;
    }

    SemanticAnalyzer semantic;
    bool validate = semantic.analyze(*ast);
    auto diagnostics = semantic.diagnostics();

    bool has_error = false;
    for (const auto& diagnostic : diagnostics) {
        std::string loc = diagnostic.file + ":" +
                          std::to_string(diagnostic.line) + ":" +
                          std::to_string(diagnostic.column) + ": ";
        if (diagnostic.severity == SemanticSeverity::Error) {
            has_error = true;
            std::cerr << "[SEMANTIC ERROR] " << loc << diagnostic.message << "\n";
        } else if (diagnostic.is_warning) {
            if (showWarnings) {
                std::cerr << "[WARNING] " << loc << diagnostic.message << "\n";
            }
        } else if (showWarnings) {
            std::cerr << loc << diagnostic.message << "\n";
        }
    }

    if (!validate || has_error) {
        std::cerr << "[ERROR][SEMANTIC] Compilation aborted due to semantic errors in '" << qFile << "'.\n";
        return false;
    }

    if (debug) {
        std::cerr << "[DEBUG][LOWERING] Lowering AST for: " << qFile << "\n";
    }
    lowerAST(ast.get());

    if (debug) {
        std::cerr << "[DEBUG][CODEGEN] Generating CRU IR for: " << qFile << "\n";
    }
    std::string outputcru = codegen(ast.get());

    fs::path outPath(croOutputFile);
    if (outPath.has_parent_path() && !outPath.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);
    }

    std::ofstream outFile(croOutputFile);
    if (!outFile) {
        std::cerr << "[ERROR][IO] Could not open CRU output file '" << croOutputFile << "' for writing.\n";
        return false;
    }
    outFile << outputcru;
    outFile.close();

    std::cout << "[CRU] Generated IR: " << croOutputFile << " (from " << qFile << ")\n";
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            printUsage(argv[0]);
            return 1;
        }

        std::vector<std::string> qFiles;
        std::vector<std::string> oFiles;
        std::string cro_output_file = "";
        std::string object_output_file = "";
        std::string binary_output_file = "";
        bool compile_only_obj = false;
        bool show_warnings = false;
        bool debug = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return 0;
            } else if (arg == "-warning") {
                show_warnings = true;
            } else if (arg == "--debug" || arg == "-v") {
                debug = true;
            } else if (arg == "-cru" || arg == "-cr") {
                if (i + 1 >= argc) {
                    std::cerr << "[ERROR][CLI] Missing filename after '" << arg << "'.\n";
                    return 1;
                }
                cro_output_file = argv[++i];
            } else if (arg == "-c") {
                compile_only_obj = true;
                if (i + 1 < argc) {
                    std::string nextArg = argv[i + 1];
                    if (!nextArg.empty() && nextArg[0] != '-' &&
                        !hasEnding(nextArg, ".q") && !hasEnding(nextArg, ".cro")) {
                        object_output_file = argv[++i];
                    }
                }
            } else if (arg == "-o") {
                if (i + 1 >= argc) {
                    std::cerr << "[ERROR][CLI] Missing filename after '-o'.\n";
                    return 1;
                }
                binary_output_file = argv[++i];
            } else if (arg == "-l") {
                if (i + 1 >= argc) {
                    std::cerr << "[ERROR][CLI] Missing target after '-l'.\n";
                    return 1;
                }
                std::string linked = argv[++i];
                if (hasEnding(linked, ".o")) {
                    oFiles.push_back(linked);
                } else if (hasEnding(linked, ".q")) {
                    qFiles.push_back(linked);
                } else {
                    std::cerr << "[ERROR][CLI] Unsupported '-l' file type: '" << linked << "' (expected .q or .o).\n";
                    return 1;
                }
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "[ERROR][CLI] Unknown option: '" << arg << "'\n";
                printUsage(argv[0]);
                return 1;
            } else {
                if (hasEnding(arg, ".q")) {
                    qFiles.push_back(arg);
                } else if (hasEnding(arg, ".o")) {
                    oFiles.push_back(arg);
                } else {
                    std::cerr << "[ERROR][CLI] Unknown input file extension for '" << arg << "' (expected .q or .o).\n";
                    return 1;
                }
            }
        }

        if (qFiles.empty()) {
            std::cerr << "[ERROR][CLI] At least one '.q' source file is required.\n";
            printUsage(argv[0]);
            return 1;
        }

        if (compile_only_obj && !binary_output_file.empty()) {
            std::cerr << "[ERROR][CLI] Cannot combine '-c' (object only) with '-o' (executable).\n";
            return 1;
        }

        // Default IR output ke a.cro bila tidak ada -cru, -c, maupun -o
        if (cro_output_file.empty() && !compile_only_obj && binary_output_file.empty() &&
            qFiles.size() == 1 && oFiles.empty()) {
            cro_output_file = "a.cro";
        }

        std::vector<std::string> generatedCroFiles;
        std::vector<std::string> tempCroFiles;

        for (std::size_t i = 0; i < qFiles.size(); ++i) {
            const std::string& qFile = qFiles[i];
            std::string targetCro;

            if (!cro_output_file.empty() && qFiles.size() == 1) {
                targetCro = cro_output_file;
            } else if (!cro_output_file.empty() && qFiles.size() > 1) {
                targetCro = stripExtension(cro_output_file) + "_" + std::to_string(i) + ".cro";
                tempCroFiles.push_back(targetCro);
            } else if (!binary_output_file.empty() || compile_only_obj) {
                targetCro = "/tmp/quelang_" + fs::path(qFile).stem().string() + "_" + std::to_string(i) + ".cro";
                tempCroFiles.push_back(targetCro);
            } else {
                targetCro = stripExtension(qFile) + ".cro";
            }

            if (!compileSingleQToCro(qFile, targetCro, show_warnings, debug)) {
                return 1;
            }
            generatedCroFiles.push_back(targetCro);
        }

        if (compile_only_obj) {
            if (object_output_file.empty()) {
                if (qFiles.size() == 1) {
                    object_output_file = stripExtension(qFiles[0]) + ".o";
                } else {
                    object_output_file = "a.o";
                }
            }

            std::string cruCmd = "./cru -cr";
            for (const auto& cro : generatedCroFiles) {
                cruCmd += " " + shellQuote(cro);
            }
            for (const auto& obj : oFiles) {
                cruCmd += " " + shellQuote(obj);
            }
            cruCmd += " -o " + shellQuote(object_output_file);

            int rc = runCommand(cruCmd, debug, "CRU-OBJ");
            if (!debug) {
                for (const auto& tmp : tempCroFiles) {
                    std::error_code ec;
                    fs::remove(tmp, ec);
                }
            }

            if (rc != 0) {
                return 1;
            }

            std::cout << "[SUCCESS] Compiled object file via ./cru: " << object_output_file << "\n";
        } else if (!binary_output_file.empty()) {
            std::string cruCmd = "./cru";
            for (const auto& cro : generatedCroFiles) {
                cruCmd += " " + shellQuote(cro);
            }
            for (const auto& obj : oFiles) {
                cruCmd += " " + shellQuote(obj);
            }
            cruCmd += " -o " + shellQuote(binary_output_file);

            int rc = runCommand(cruCmd, debug, "CRU-LINK");
            if (!debug) {
                for (const auto& tmp : tempCroFiles) {
                    std::error_code ec;
                    fs::remove(tmp, ec);
                }
            }

            if (rc != 0) {
                return 1;
            }

            std::cout << "[SUCCESS] Built executable via ./cru: " << binary_output_file << "\n";
        } else if (!cro_output_file.empty() && qFiles.size() > 1) {
            std::ofstream merged(cro_output_file);
            if (!merged) {
                std::cerr << "[ERROR][IO] Could not open merged CRU output '" << cro_output_file << "'.\n";
                return 1;
            }
            for (const auto& cro : generatedCroFiles) {
                std::ifstream in(cro);
                merged << in.rdbuf() << "\n";
                if (!debug) {
                    std::error_code ec;
                    fs::remove(cro, ec);
                }
            }
            std::cout << "[CRU] Merged IR written to " << cro_output_file << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL EXCEPTION] " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[FATAL EXCEPTION] Unknown crash / exception (possible null pointer or segfault).\n";
        return 1;
    }
}
