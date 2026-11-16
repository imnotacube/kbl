#include "builtins.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common.h"

namespace {

// ------------------------------------------------------------------ helpers

void arity(const std::vector<Value>& a, size_t lo, size_t hi, const char* fn, int line, int col) {
    if (a.size() >= lo && a.size() <= hi) return;
    std::string want;
    if (lo == hi) {
        want = std::to_string(lo);
    } else {
        want = std::to_string(lo) + ".." + std::to_string(hi);
    }
    throw ScriptError(line, col, std::string(fn) + ": ожидалось аргументов " + want + ", получено " +
                                    std::to_string(a.size()));
}

long long num(const Value& v, const char* fn, int line, int col) {
    if (v.t == VT::Str) {
        const char* s = v.s.c_str();
        char* end = nullptr;
        long long r = std::strtoll(s, &end, 10);
        if (end == s) throw ScriptError(line, col, std::string(fn) + ": строка не является числом");
        return r;
    }
    return v.asInt();
}

double numf(const Value& v, const char* fn, int line, int col) {
    if (v.t == VT::Str) {
        const char* s = v.s.c_str();
        char* end = nullptr;
        double r = std::strtod(s, &end);
        if (end == s) throw ScriptError(line, col, std::string(fn) + ": строка не является числом");
        return r;
    }
    return v.asDouble();
}

const std::string& text(const Value& v) { return v.s; }

std::string trimCopy(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

void writeOut(const std::string& s) {
    if (!s.empty()) std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

std::string readLine() {
    std::string line;
    if (!std::getline(std::cin, line)) return "";
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

// ----------------------------------------------------------------- keyboard

const std::unordered_map<std::string, WORD>& keyTable() {
    static const std::unordered_map<std::string, WORD> table = [] {
        std::unordered_map<std::string, WORD> m;
        for (char c = 'a'; c <= 'z'; ++c) m[std::string(1, c)] = (WORD)(c - 'a' + 'A');
        for (char c = '0'; c <= '9'; ++c) m[std::string(1, c)] = (WORD)c;
        m["enter"] = VK_RETURN;
        m["return"] = VK_RETURN;
        m["esc"] = VK_ESCAPE;
        m["escape"] = VK_ESCAPE;
        m["tab"] = VK_TAB;
        m["space"] = VK_SPACE;
        m["backspace"] = VK_BACK;
        m["back"] = VK_BACK;
        m["delete"] = VK_DELETE;
        m["del"] = VK_DELETE;
        m["insert"] = VK_INSERT;
        m["ins"] = VK_INSERT;
        m["home"] = VK_HOME;
        m["end"] = VK_END;
        m["pageup"] = VK_PRIOR;
        m["pgup"] = VK_PRIOR;
        m["pagedown"] = VK_NEXT;
        m["pgdn"] = VK_NEXT;
        m["up"] = VK_UP;
        m["down"] = VK_DOWN;
        m["left"] = VK_LEFT;
        m["right"] = VK_RIGHT;
        m["shift"] = VK_SHIFT;
        m["ctrl"] = VK_CONTROL;
        m["control"] = VK_CONTROL;
        m["alt"] = VK_MENU;
        m["win"] = VK_LWIN;
        m["lwin"] = VK_LWIN;
        m["rwin"] = VK_RWIN;
        m["capslock"] = VK_CAPITAL;
        m["numlock"] = VK_NUMLOCK;
        m["scrolllock"] = VK_SCROLL;
        m["printscreen"] = VK_SNAPSHOT;
        m["prtsc"] = VK_SNAPSHOT;
        m["pause"] = VK_PAUSE;
        m["apps"] = VK_APPS;
        m["minus"] = VK_OEM_MINUS;
        m["plus"] = VK_OEM_PLUS;
        m["equal"] = VK_OEM_PLUS;
        m["comma"] = VK_OEM_COMMA;
        m["period"] = VK_OEM_PERIOD;
        m["dot"] = VK_OEM_PERIOD;
        m["slash"] = VK_OEM_2;
        m["backslash"] = VK_OEM_5;
        m["semicolon"] = VK_OEM_1;
        m["quote"] = VK_OEM_7;
        m["grave"] = VK_OEM_3;
        m["lbracket"] = VK_OEM_4;
        m["rbracket"] = VK_OEM_6;
        for (int i = 1; i <= 24; ++i) m["f" + std::to_string(i)] = (WORD)(VK_F1 + i - 1);
        return m;
    }();
    return table;
}

bool isModifierName(const std::string& n) {
    return n == "ctrl" || n == "control" || n == "shift" || n == "alt" || n == "win" || n == "lwin" ||
           n == "rwin";
}

bool isExtendedKey(WORD vk) {
    switch (vk) {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_SNAPSHOT:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
            return true;
        default:
            return false;
    }
}

void pushKey(std::vector<INPUT>& v, WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (isExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    v.push_back(in);
}

void sendAll(std::vector<INPUT>& v) {
    if (v.empty()) return;
    SendInput((UINT)v.size(), v.data(), sizeof(INPUT));
    v.clear();
}

bool parseCombo(const std::string& combo, std::vector<WORD>& mods, WORD& key, std::string& bad) {
    mods.clear();
    key = 0;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : combo) {
        if (c == '+') {
            parts.push_back(trimCopy(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(trimCopy(cur));

    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty()) continue;
        std::string name = lowerCopy(parts[i]);
        auto it = keyTable().find(name);
        if (it == keyTable().end()) {
            bad = parts[i];
            return false;
        }
        if (isModifierName(name) && i + 1 < parts.size()) {
            mods.push_back(it->second);
        } else {
            key = it->second;
        }
    }
    if (key == 0) {
        bad = combo;
        return false;
    }
    return true;
}

void keyCombo(const std::string& combo, bool down, bool up, int line, int col) {
    std::vector<WORD> mods;
    WORD key = 0;
    std::string bad;
    if (!parseCombo(combo, mods, key, bad)) {
        throw ScriptError(line, col, "неизвестная клавиша или комбинация: '" + bad + "'");
    }
    std::vector<INPUT> v;
    if (down) {
        for (WORD m : mods) pushKey(v, m, true);
        pushKey(v, key, true);
    }
    if (up) {
        pushKey(v, key, false);
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) pushKey(v, *it, false);
    }
    sendAll(v);
}

void sendText(const std::string& utf8, long long perCharDelayMs) {
    std::wstring w = utf8ToWide(utf8);
    std::vector<INPUT> batch;
    batch.reserve(512);
    auto flush = [&] {
        sendAll(batch);
    };
    for (wchar_t ch : w) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            pushKey(batch, VK_RETURN, true);
            pushKey(batch, VK_RETURN, false);
        } else if (ch == L'\t') {
            pushKey(batch, VK_TAB, true);
            pushKey(batch, VK_TAB, false);
        } else {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = 0;
            in.ki.wScan = ch;
            in.ki.dwFlags = KEYEVENTF_UNICODE;
            batch.push_back(in);
            in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            batch.push_back(in);
        }
        if (perCharDelayMs > 0) {
            flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(perCharDelayMs));
        } else if (batch.size() >= 400) {
            flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    flush();
}

// -------------------------------------------------------------------- mouse

void mouseMoveAbs(int x, int y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 1) vw = 2;
    if (vh <= 1) vh = 2;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in.mi.dx = (LONG)std::llround((double)(x - vx) * 65535.0 / (double)(vw - 1));
    in.mi.dy = (LONG)std::llround((double)(y - vy) * 65535.0 / (double)(vh - 1));
    std::vector<INPUT> v{in};
    sendAll(v);
}

void mouseMoveSmooth(int x, int y, long long durationMs) {
    if (durationMs <= 0) {
        mouseMoveAbs(x, y);
        return;
    }
    POINT p{};
    GetCursorPos(&p);
    long long steps = durationMs / 10;
    if (steps < 1) steps = 1;
    if (steps > 1000) steps = 1000;
    long long delay = durationMs / steps;
    for (long long i = 1; i <= steps; ++i) {
        double t = (double)i / (double)steps;
        int cx = (int)std::llround(p.x + (x - p.x) * t);
        int cy = (int)std::llround(p.y + (y - p.y) * t);
        mouseMoveAbs(cx, cy);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

struct ButtonFlags {
    DWORD down = 0;
    DWORD up = 0;
    DWORD data = 0;
};

bool buttonFlags(const std::string& raw, ButtonFlags& bf) {
    std::string n = lowerCopy(trimCopy(raw));
    if (n.empty() || n == "left" || n == "l") {
        bf.down = MOUSEEVENTF_LEFTDOWN;
        bf.up = MOUSEEVENTF_LEFTUP;
        return true;
    }
    if (n == "right" || n == "r") {
        bf.down = MOUSEEVENTF_RIGHTDOWN;
        bf.up = MOUSEEVENTF_RIGHTUP;
        return true;
    }
    if (n == "middle" || n == "m" || n == "wheel") {
        bf.down = MOUSEEVENTF_MIDDLEDOWN;
        bf.up = MOUSEEVENTF_MIDDLEUP;
        return true;
    }
    if (n == "x1" || n == "back") {
        bf.down = MOUSEEVENTF_XDOWN;
        bf.up = MOUSEEVENTF_XUP;
        bf.data = XBUTTON1;
        return true;
    }
    if (n == "x2" || n == "forward") {
        bf.down = MOUSEEVENTF_XDOWN;
        bf.up = MOUSEEVENTF_XUP;
        bf.data = XBUTTON2;
        return true;
    }
    return false;
}

void mouseButton(DWORD flag, DWORD data) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    in.mi.mouseData = data;
    std::vector<INPUT> v{in};
    sendAll(v);
}

// ---------------------------------------------------------------- clipboard

std::string clipGet() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = wideToUtf8(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

bool clipSet(const std::string& utf8) {
    std::wstring w = utf8ToWide(utf8);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        CloseClipboard();
        return false;
    }
    void* p = GlobalLock(h);
    if (!p) {
        GlobalFree(h);
        CloseClipboard();
        return false;
    }
    std::memcpy(p, w.c_str(), bytes);
    GlobalUnlock(h);
    if (!SetClipboardData(CF_UNICODETEXT, h)) {
        GlobalFree(h);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

// ----------------------------------------------------------------- windows

struct FindState {
    std::wstring needle;
    bool exact = false;
    HWND found = nullptr;
};

BOOL CALLBACK findProc(HWND h, LPARAM lp) {
    FindState* st = (FindState*)lp;
    if (!IsWindowVisible(h)) return TRUE;
    wchar_t buf[1024];
    int n = GetWindowTextW(h, buf, 1024);
    if (n > 0) {
        std::wstring title(buf, (size_t)n);
        CharLowerBuffW(title.data(), (DWORD)title.size());
        bool hit = st->exact ? (title == st->needle) : (title.find(st->needle) != std::wstring::npos);
        if (hit) {
            st->found = h;
            return FALSE;
        }
    }
    return TRUE;
}

HWND findWindow(const std::string& rawNeedle, bool exact) {
    FindState st;
    st.exact = exact;
    st.needle = utf8ToWide(rawNeedle);
    if (st.needle.empty()) return nullptr;
    CharLowerBuffW(st.needle.data(), (DWORD)st.needle.size());
    EnumWindows(findProc, (LPARAM)&st);
    return st.found;
}

void activateWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (SetForegroundWindow(hwnd)) return;
    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD thisThread = GetCurrentThreadId();
    if (fgThread && fgThread != thisThread) {
        AttachThreadInput(thisThread, fgThread, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        AttachThreadInput(thisThread, fgThread, FALSE);
    } else {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }
}

// ---------------------------------------------------------------- processes

int runProcess(const std::string& cmd, bool wait, DWORD& pid) {
    std::wstring wcmd = utf8ToWide(cmd);
    if (wcmd.empty()) return -1;
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    pid = pi.dwProcessId;
    int code = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD c = 0;
        if (GetExitCodeProcess(pi.hProcess, &c)) code = (int)c;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// ------------------------------------------------------------------ strings

std::wstring upperWide(std::wstring w) {
    if (!w.empty()) CharUpperBuffW(w.data(), (DWORD)w.size());
    return w;
}

std::wstring lowerWide(std::wstring w) {
    if (!w.empty()) CharLowerBuffW(w.data(), (DWORD)w.size());
    return w;
}

unsigned firstCodepoint(const std::wstring& w) {
    if (w.empty()) return 0;
    wchar_t c = w[0];
    if (c >= 0xD800 && c <= 0xDBFF && w.size() > 1) {
        wchar_t c2 = w[1];
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
            return 0x10000u + (((unsigned)(c - 0xD800)) << 10) + (unsigned)(c2 - 0xDC00);
        }
    }
    return (unsigned)c;
}

std::string codepointToUtf8(unsigned cp) {
    std::wstring w;
    if (cp <= 0xFFFF) {
        w.push_back((wchar_t)cp);
    } else if (cp <= 0x10FFFF) {
        cp -= 0x10000;
        w.push_back((wchar_t)(0xD800 + (cp >> 10)));
        w.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
    } else {
        return "";
    }
    return wideToUtf8(w);
}

std::mt19937_64& rng() {
    static std::mt19937_64 g{std::random_device{}()};
    return g;
}

long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// ------------------------------------------------------------------- lookup

int builtinId(const std::string& name) {
    static const std::unordered_map<std::string, int> m = {
        {"print", B_print},
        {"println", B_println},
        {"sleep", B_sleep},
        {"time_ms", B_time_ms},
        {"time_s", B_time_s},
        {"type", B_type},
        {"key_press", B_key_press},
        {"key_down", B_key_down},
        {"key_up", B_key_up},
        {"key_state", B_key_state},
        {"mouse_move", B_mouse_move},
        {"mouse_move_rel", B_mouse_move_rel},
        {"mouse_x", B_mouse_x},
        {"mouse_y", B_mouse_y},
        {"mouse_click", B_mouse_click},
        {"mouse_down", B_mouse_down},
        {"mouse_up", B_mouse_up},
        {"mouse_wheel", B_mouse_wheel},
        {"mouse_wheel_h", B_mouse_wheel_h},
        {"screen_width", B_screen_width},
        {"screen_height", B_screen_height},
        {"pixel_color", B_pixel_color},
        {"clip_get", B_clip_get},
        {"clip_set", B_clip_set},
        {"input", B_input},
        {"exit", B_exit},
        {"run", B_run},
        {"start", B_start},
        {"kill", B_kill},
        {"msgbox", B_msgbox},
        {"window_find", B_window_find},
        {"window_active", B_window_active},
        {"window_activate", B_window_activate},
        {"window_title", B_window_title},
        {"window_wait", B_window_wait},
        {"window_close", B_window_close},
        {"window_exists", B_window_exists},
        {"random", B_random},
        {"random_float", B_random_float},
        {"to_str", B_to_str},
        {"to_int", B_to_int},
        {"to_float", B_to_float},
        {"to_bool", B_to_bool},
        {"len", B_len},
        {"upper", B_upper},
        {"lower", B_lower},
        {"trim", B_trim},
        {"replace", B_replace},
        {"contains", B_contains},
        {"substr", B_substr},
        {"ord", B_ord},
        {"chr", B_chr},
        {"typeof", B_typeof},
    };
    auto it = m.find(name);
    return it == m.end() ? -1 : it->second;
}

// ------------------------------------------------------------------ builtins

Value callBuiltin(int id, std::vector<Value>& args, int line, int col) {
    switch (id) {
        case B_print:
        case B_println: {
            std::string out;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) out.push_back(' ');
                out += args[i].toString();
            }
            if (id == B_println) out.push_back('\n');
            writeOut(out);
            return Value::null();
        }

        case B_sleep: {
            arity(args, 1, 1, "sleep", line, col);
            double ms = numf(args[0], "sleep", line, col);
            if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds((long long)ms));
            return Value::null();
        }

        case B_time_ms:
            arity(args, 0, 0, "time_ms", line, col);
            return Value::integer(nowMs());

        case B_time_s:
            arity(args, 0, 0, "time_s", line, col);
            return Value::number((double)nowMs() / 1000.0);

        case B_type: {
            arity(args, 1, 2, "type", line, col);
            long long delay = args.size() > 1 ? num(args[1], "type", line, col) : 0;
            sendText(args[0].toString(), delay);
            return Value::null();
        }

        case B_key_press: {
            arity(args, 1, 1, "key_press", line, col);
            keyCombo(args[0].toString(), true, true, line, col);
            return Value::null();
        }

        case B_key_down: {
            arity(args, 1, 1, "key_down", line, col);
            keyCombo(args[0].toString(), true, false, line, col);
            return Value::null();
        }

        case B_key_up: {
            arity(args, 1, 1, "key_up", line, col);
            keyCombo(args[0].toString(), false, true, line, col);
            return Value::null();
        }

        case B_key_state: {
            arity(args, 1, 1, "key_state", line, col);
            std::vector<WORD> mods;
            WORD key = 0;
            std::string bad;
            if (!parseCombo(args[0].toString(), mods, key, bad)) {
                throw ScriptError(line, col, "неизвестная клавиша: '" + bad + "'");
            }
            for (WORD m : mods) {
                if ((GetAsyncKeyState(m) & 0x8000) == 0) return Value::boolean(false);
            }
            return Value::boolean((GetAsyncKeyState(key) & 0x8000) != 0);
        }

        case B_mouse_move: {
            arity(args, 2, 3, "mouse_move", line, col);
            int x = (int)num(args[0], "mouse_move", line, col);
            int y = (int)num(args[1], "mouse_move", line, col);
            long long dur = args.size() > 2 ? num(args[2], "mouse_move", line, col) : 0;
            mouseMoveSmooth(x, y, dur);
            return Value::null();
        }

        case B_mouse_move_rel: {
            arity(args, 2, 2, "mouse_move_rel", line, col);
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_MOVE;
            in.mi.dx = (LONG)num(args[0], "mouse_move_rel", line, col);
            in.mi.dy = (LONG)num(args[1], "mouse_move_rel", line, col);
            std::vector<INPUT> v{in};
            sendAll(v);
            return Value::null();
        }

        case B_mouse_x: {
            arity(args, 0, 0, "mouse_x", line, col);
            POINT p{};
            GetCursorPos(&p);
            return Value::integer(p.x);
        }

        case B_mouse_y: {
            arity(args, 0, 0, "mouse_y", line, col);
            POINT p{};
            GetCursorPos(&p);
            return Value::integer(p.y);
        }

        case B_mouse_click: {
            arity(args, 0, 3, "mouse_click", line, col);
            std::string button = "left";
            size_t idx = 0;
            if (!args.empty() && args[0].t == VT::Str) {
                button = args[0].s;
                idx = 1;
            }
            ButtonFlags bf;
            if (!buttonFlags(button, bf)) {
                throw ScriptError(line, col, "неизвестная кнопка мыши: '" + button + "'");
            }
            if (args.size() - idx == 2) {
                mouseMoveAbs((int)num(args[idx], "mouse_click", line, col),
                             (int)num(args[idx + 1], "mouse_click", line, col));
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            } else if (args.size() - idx != 0) {
                throw ScriptError(line, col, "mouse_click: ожидалось 'кнопка, x, y'");
            }
            mouseButton(bf.down, bf.data);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            mouseButton(bf.up, 0);
            return Value::null();
        }

        case B_mouse_down:
        case B_mouse_up: {
            const char* fn = id == B_mouse_down ? "mouse_down" : "mouse_up";
            arity(args, 0, 1, fn, line, col);
            std::string button = args.empty() ? "left" : args[0].toString();
            ButtonFlags bf;
            if (!buttonFlags(button, bf)) {
                throw ScriptError(line, col, "неизвестная кнопка мыши: '" + button + "'");
            }
            mouseButton(id == B_mouse_down ? bf.down : bf.up, bf.data);
            return Value::null();
        }

        case B_mouse_wheel:
        case B_mouse_wheel_h: {
            const char* fn = id == B_mouse_wheel ? "mouse_wheel" : "mouse_wheel_h";
            arity(args, 1, 1, fn, line, col);
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = id == B_mouse_wheel ? MOUSEEVENTF_WHEEL : MOUSEEVENTF_HWHEEL;
            in.mi.mouseData = (DWORD)(num(args[0], fn, line, col) * WHEEL_DELTA);
            std::vector<INPUT> v{in};
            sendAll(v);
            return Value::null();
        }

        case B_screen_width:
            arity(args, 0, 0, "screen_width", line, col);
            return Value::integer(GetSystemMetrics(SM_CXSCREEN));

        case B_screen_height:
            arity(args, 0, 0, "screen_height", line, col);
            return Value::integer(GetSystemMetrics(SM_CYSCREEN));

        case B_pixel_color: {
            arity(args, 2, 2, "pixel_color", line, col);
            int x = (int)num(args[0], "pixel_color", line, col);
            int y = (int)num(args[1], "pixel_color", line, col);
            HDC hdc = GetDC(nullptr);
            if (!hdc) throw ScriptError(line, col, "pixel_color: не удалось получить DC экрана");
            COLORREF c = GetPixel(hdc, x, y);
            ReleaseDC(nullptr, hdc);
            if (c == CLR_INVALID) return Value::integer(-1);
            long long rgb = ((long long)GetRValue(c) << 16) | ((long long)GetGValue(c) << 8) | GetBValue(c);
            return Value::integer(rgb);
        }

        case B_clip_get:
            arity(args, 0, 0, "clip_get", line, col);
            return Value::str(clipGet());

        case B_clip_set:
            arity(args, 1, 1, "clip_set", line, col);
            return Value::boolean(clipSet(args[0].toString()));

        case B_input: {
            arity(args, 0, 1, "input", line, col);
            if (!args.empty()) writeOut(args[0].toString());
            return Value::str(readLine());
        }

        case B_exit: {
            arity(args, 0, 1, "exit", line, col);
            int code = args.empty() ? 0 : (int)num(args[0], "exit", line, col);
            std::fflush(stdout);
            std::exit(code);
        }

        case B_run: {
            arity(args, 1, 1, "run", line, col);
            DWORD pid = 0;
            int code = runProcess(args[0].toString(), true, pid);
            if (code < 0) throw ScriptError(line, col, "run: не удалось запустить процесс");
            return Value::integer(code);
        }

        case B_start: {
            arity(args, 1, 1, "start", line, col);
            DWORD pid = 0;
            int code = runProcess(args[0].toString(), false, pid);
            if (code < 0) throw ScriptError(line, col, "start: не удалось запустить процесс");
            return Value::integer((long long)pid);
        }

        case B_kill: {
            arity(args, 1, 1, "kill", line, col);
            DWORD pid = (DWORD)num(args[0], "kill", line, col);
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (!h) return Value::boolean(false);
            BOOL ok = TerminateProcess(h, 1);
            CloseHandle(h);
            return Value::boolean(ok != 0);
        }

        case B_msgbox: {
            arity(args, 1, 2, "msgbox", line, col);
            std::wstring title = args.size() > 1 ? utf8ToWide(args[0].toString()) : L"kbl";
            std::wstring body = utf8ToWide(args.back().toString());
            int r = MessageBoxW(nullptr, body.c_str(), title.c_str(), MB_OK);
            return Value::integer(r);
        }

        case B_window_find: {
            arity(args, 1, 2, "window_find", line, col);
            bool exact = args.size() > 1 && args[1].truthy();
            HWND h = findWindow(args[0].toString(), exact);
            return Value::integer((long long)(intptr_t)h);
        }

        case B_window_active: {
            arity(args, 0, 0, "window_active", line, col);
            return Value::integer((long long)(intptr_t)GetForegroundWindow());
        }

        case B_window_activate: {
            arity(args, 1, 1, "window_activate", line, col);
            HWND h = (HWND)(intptr_t)num(args[0], "window_activate", line, col);
            if (!h || !IsWindow(h)) return Value::boolean(false);
            activateWindow(h);
            return Value::boolean(GetForegroundWindow() == h);
        }

        case B_window_title: {
            arity(args, 1, 1, "window_title", line, col);
            HWND h = (HWND)(intptr_t)num(args[0], "window_title", line, col);
            if (!h || !IsWindow(h)) return Value::str("");
            wchar_t buf[1024];
            int n = GetWindowTextW(h, buf, 1024);
            return Value::str(wideToUtf8(std::wstring(buf, (size_t)(n > 0 ? n : 0))));
        }

        case B_window_wait: {
            arity(args, 1, 2, "window_wait", line, col);
            std::string needle = args[0].toString();
            long long timeout = args.size() > 1 ? num(args[1], "window_wait", line, col) : 10000;
            long long start = nowMs();
            for (;;) {
                HWND h = findWindow(needle, false);
                if (h) return Value::integer((long long)(intptr_t)h);
                if (nowMs() - start >= timeout) return Value::integer(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        case B_window_close: {
            arity(args, 1, 1, "window_close", line, col);
            HWND h = (HWND)(intptr_t)num(args[0], "window_close", line, col);
            if (!h || !IsWindow(h)) return Value::boolean(false);
            PostMessageW(h, WM_CLOSE, 0, 0);
            return Value::boolean(true);
        }

        case B_window_exists: {
            arity(args, 1, 1, "window_exists", line, col);
            HWND h = (HWND)(intptr_t)num(args[0], "window_exists", line, col);
            return Value::boolean(h && IsWindow(h) != 0);
        }

        case B_random: {
            arity(args, 2, 2, "random", line, col);
            long long lo = num(args[0], "random", line, col);
            long long hi = num(args[1], "random", line, col);
            if (lo > hi) std::swap(lo, hi);
            std::uniform_int_distribution<long long> d(lo, hi);
            return Value::integer(d(rng()));
        }

        case B_random_float: {
            arity(args, 0, 2, "random_float", line, col);
            double lo = 0.0;
            double hi = 1.0;
            if (args.size() == 2) {
                lo = numf(args[0], "random_float", line, col);
                hi = numf(args[1], "random_float", line, col);
                if (lo > hi) std::swap(lo, hi);
            }
            std::uniform_real_distribution<double> d(lo, hi);
            return Value::number(d(rng()));
        }

        case B_to_str:
            arity(args, 1, 1, "to_str", line, col);
            return Value::str(args[0].toString());

        case B_to_int:
            arity(args, 1, 1, "to_int", line, col);
            return Value::integer(num(args[0], "to_int", line, col));

        case B_to_float:
            arity(args, 1, 1, "to_float", line, col);
            return Value::number(numf(args[0], "to_float", line, col));

        case B_to_bool:
            arity(args, 1, 1, "to_bool", line, col);
            return Value::boolean(args[0].truthy());

        case B_len: {
            arity(args, 1, 1, "len", line, col);
            return Value::integer((long long)utf8Length(args[0].toString()));
        }

        case B_upper:
            arity(args, 1, 1, "upper", line, col);
            return Value::str(wideToUtf8(upperWide(utf8ToWide(args[0].toString()))));

        case B_lower:
            arity(args, 1, 1, "lower", line, col);
            return Value::str(wideToUtf8(lowerWide(utf8ToWide(args[0].toString()))));

        case B_trim: {
            arity(args, 1, 1, "trim", line, col);
            return Value::str(trimCopy(args[0].toString()));
        }

        case B_replace: {
            arity(args, 3, 3, "replace", line, col);
            std::string s = args[0].toString();
            const std::string& from = args[1].toString();
            const std::string& to = args[2].toString();
            if (from.empty()) return Value::str(s);
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return Value::str(s);
        }

        case B_contains: {
            arity(args, 2, 2, "contains", line, col);
            return Value::boolean(args[0].toString().find(args[1].toString()) != std::string::npos);
        }

        case B_substr: {
            arity(args, 2, 3, "substr", line, col);
            std::wstring w = utf8ToWide(args[0].toString());
            long long start = num(args[1], "substr", line, col);
            if (start < 0) start = 0;
            if ((size_t)start > w.size()) return Value::str("");
            long long len = args.size() > 2 ? num(args[2], "substr", line, col) : (long long)w.size() - start;
            if (len < 0) len = 0;
            if ((size_t)(start + len) > w.size()) len = (long long)w.size() - start;
            return Value::str(wideToUtf8(w.substr((size_t)start, (size_t)len)));
        }

        case B_ord: {
            arity(args, 1, 1, "ord", line, col);
            return Value::integer((long long)firstCodepoint(utf8ToWide(args[0].toString())));
        }

        case B_chr: {
            arity(args, 1, 1, "chr", line, col);
            return Value::str(codepointToUtf8((unsigned)num(args[0], "chr", line, col)));
        }

        case B_typeof:
            arity(args, 1, 1, "typeof", line, col);
            return Value::str(args[0].typeName());
    }
    throw ScriptError(line, col, "внутренняя ошибка: неизвестная встроенная функция");
}
