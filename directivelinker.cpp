#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <regex>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot write file: " + path.string());
    }
    file << content;
}

std::string loadLibPath(const fs::path& configPath = "config.crof") {
    static std::string cachedLibPath;
    static bool loaded = false;

    if (loaded) return cachedLibPath;
    loaded = true;

    if (!fs::exists(configPath)) return "";

    std::string content = readFile(configPath);
    std::regex libRegex(R"REGEX(libpath\s*=\s*"([^"]+)")REGEX");
    std::smatch match;

    if (std::regex_search(content, match, libRegex)) {
        cachedLibPath = match[1].str();
        if (!cachedLibPath.empty() && cachedLibPath.back() != '/')
            cachedLibPath += '/';
    }
    return cachedLibPath;
}

std::string processFile(
    const fs::path& filePath,
    std::unordered_set<std::string>& loadedFiles
) {
    fs::path absPath = fs::absolute(filePath);

    if (!loadedFiles.insert(absPath.string()).second) {
        return "";
    }

    std::string content = readFile(absPath);
    std::istringstream input(content);
    std::string line;
    std::string result;
    std::string code;

    static const std::regex loadQuoted(R"(import\s+["']([^"']+)["'])");
    static const std::regex loadBracket(R"(import\s+(?:\[)?([a-zA-Z_][a-zA-Z0-9_]*)\]?)");

    while (std::getline(input, line)) {
        std::smatch match;
        fs::path depPath;

        if (std::regex_search(line, match, loadQuoted)) {
            depPath = match[1].str();
        }
        else if (std::regex_search(line, match, loadBracket)) {
            std::string libPath = loadLibPath();
            if (libPath.empty()) {
                throw std::runtime_error(
                    "libpath not found for import " + match[1].str()
                );
            }
            depPath = fs::path(libPath) / (match[1].str() + ".q");
        }
        else {
            code += line + "\n";
            continue;
        }

        fs::path resolved = absPath.parent_path() / depPath;
        result += processFile(resolved, loadedFiles);
    }

    result += "@line " + absPath.string() + "\n";
    result += code;
    return result;
}

std::string preprocess(const fs::path& entryFile) {
    std::unordered_set<std::string> loadedFiles;
    return processFile(entryFile, loadedFiles);
}
