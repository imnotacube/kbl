#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "common.h"
#include "errors.h"
#include "interp.h"
#include "lexer.h"
#include "parser.h"

namespace {

const char* kVersion = "kbl 1.0";

void setDpiAware() {
    using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto fn = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

DWORD WINAPI safetyThread(LPVOID) {
    for (;;) {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (ctrl && alt && esc) {
            const char* msg = "\nkbl: остановлено пользователем (Ctrl+Alt+Esc)\n";
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)std::strlen(msg), &written, nullptr);
            ExitProcess(130);
        }
        Sleep(25);
    }
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? path : path.substr(p + 1);
}

void printHelp() {
    std::printf(
        "%s\n"
        "\n"
        "Использование:\n"
        "  kbl <файл.kbl>            выполнить скрипт\n"
        "  kbl --check <файл.kbl>    только проверить синтаксис\n"
        "  kbl --no-safety <файл>    без аварийной остановки Ctrl+Alt+Esc\n"
        "  kbl --version             версия\n"
        "  kbl --help                эта справка\n"
        "\n"
        "Аварийная остановка во время работы: Ctrl+Alt+Esc\n",
        kVersion);
}

void printSourceLine(const std::string& src, int line, int col) {
    int cur = 1;
    size_t start = 0;
    while (cur < line && start < src.size()) {
        if (src[start] == '\n') ++cur;
        ++start;
    }
    if (cur != line) return;

    size_t end = src.find('\n', start);
    if (end == std::string::npos) end = src.size();
    std::string text = src.substr(start, end - start);
    if (!text.empty() && text.back() == '\r') text.pop_back();

    std::fprintf(stderr, "  %s\n  ", text.c_str());
    for (int i = 1; i < col && i - 1 < (int)text.size(); ++i) {
        std::fputc(text[(size_t)(i - 1)] == '\t' ? '\t' : ' ', stderr);
    }
    std::fputs("^\n", stderr);
}

void reportError(const ScriptError& e, const std::string& path, const std::string& src) {
    std::fprintf(stderr, "%s:%d:%d: ошибка: %s\n", path.c_str(), e.line, e.col, e.msg.c_str());
    printSourceLine(src, e.line, e.col);
}

bool readScript(const std::wstring& path, std::string& out, std::string& error) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        error = "не удалось открыть файл";
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 2) {
        unsigned char b0 = (unsigned char)data[0];
        unsigned char b1 = (unsigned char)data[1];
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
            error = "файл сохранён в UTF-16, пересохраните его в UTF-8";
            return false;
        }
    }
    if (data.size() >= 3) {
        unsigned char b0 = (unsigned char)data[0];
        unsigned char b1 = (unsigned char)data[1];
        unsigned char b2 = (unsigned char)data[2];
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) data.erase(0, 3);
    }
    out = std::move(data);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setDpiAware();

    bool checkOnly = false;
    bool noSafety = false;
    std::wstring scriptPath;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"-h" || a == L"--help" || a == L"/?") {
            printHelp();
            return 0;
        }
        if (a == L"-v" || a == L"--version") {
            std::printf("%s\n", kVersion);
            return 0;
        }
        if (a == L"--check") {
            checkOnly = true;
            continue;
        }
        if (a == L"--no-safety") {
            noSafety = true;
            continue;
        }
        if (!a.empty() && a[0] == L'-') {
            std::fprintf(stderr, "kbl: неизвестный параметр '%s'\n", wideToUtf8(a).c_str());
            return 2;
        }
        if (!scriptPath.empty()) {
            std::fprintf(stderr, "kbl: можно указать только один скрипт\n");
            return 2;
        }
        scriptPath = a;
    }

    if (scriptPath.empty()) {
        printHelp();
        return 2;
    }

    std::string src;
    std::string readError;
    if (!readScript(scriptPath, src, readError)) {
        std::fprintf(stderr, "kbl: %s: %s\n", wideToUtf8(scriptPath).c_str(), readError.c_str());
        return 2;
    }

    const std::string displayPath = wideToUtf8(scriptPath);
    std::wstring title = L"kbl - " + std::wstring(scriptPath.substr(scriptPath.find_last_of(L"\\/") + 1));
    SetConsoleTitleW(title.c_str());

    try {
        std::vector<Token> tokens = lex(src);
        Program program = parse(tokens, displayPath);

        if (checkOnly) {
            std::printf("kbl: %s: синтаксис в порядке\n", baseName(displayPath).c_str());
            return 0;
        }

        if (!noSafety) {
            HANDLE h = CreateThread(nullptr, 0, safetyThread, nullptr, 0, nullptr);
            if (h) CloseHandle(h);
        }

        Interp interp(program);
        interp.run();
    } catch (const ScriptError& e) {
        reportError(e, displayPath, src);
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kbl: внутренняя ошибка: %s\n", e.what());
        return 3;
    }

    return 0;
}
