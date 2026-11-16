#include "parser.h"

#include <algorithm>
#include <unordered_map>

#include "builtins.h"

namespace {

struct Slot {
    int index = -1;
    bool global = false;
};

struct PendingCall {
    Expr* node;
    std::string name;
    int line;
    int col;
};

bool isTypeKeyword(const std::string& s) {
    return s == "int" || s == "float" || s == "double" || s == "string" || s == "bool" ||
           s == "var" || s == "auto" || s == "void" || s == "num";
}

class Parser {
public:
    Parser(const std::vector<Token>& tokens, std::string path)
        : toks_(tokens), path_(std::move(path)) {}

    Program parse() {
        scopes_.emplace_back();
        while (!at(Tok::End)) {
            prog_.globals.push_back(parseStatement());
        }
        prog_.globalMaxSlots = nextGlobal_;
        prog_.path = path_;

        for (const PendingCall& pc : pending_) {
            auto f = funcIds_.find(pc.name);
            if (f != funcIds_.end()) {
                pc.node->funcId = f->second;
                continue;
            }
            int id = builtinId(pc.name);
            if (id >= 0) {
                pc.node->builtinId = id;
                continue;
            }
            err(pc.line, pc.col, "неизвестная функция '" + pc.name + "'");
        }
        return std::move(prog_);
    }

private:
    const std::vector<Token>& toks_;
    std::string path_;
    size_t pos_ = 0;

    Program prog_;

    std::unordered_map<std::string, int> globals_;
    std::vector<std::unordered_map<std::string, Slot>> scopes_;

    int nextGlobal_ = 0;
    int nextLocal_ = 0;
    bool inFunc_ = false;
    int loopDepth_ = 0;
    int funcDepth_ = 0;

    std::unordered_map<std::string, int> funcIds_;
    std::vector<PendingCall> pending_;

    // ---------------------------------------------------------------- tokens

    const Token& cur() const { return toks_[pos_]; }

    const Token& peek(size_t n = 1) const {
        size_t p = pos_ + n;
        if (p >= toks_.size()) p = toks_.size() - 1;
        return toks_[p];
    }

    bool at(Tok k) const { return cur().kind == k; }

    bool atIdent(const char* s) const {
        return cur().kind == Tok::Ident && cur().text == s;
    }

    Token advance() {
        Token t = toks_[pos_];
        if (pos_ + 1 < toks_.size()) ++pos_;
        return t;
    }

    bool match(Tok k) {
        if (at(k)) {
            advance();
            return true;
        }
        return false;
    }

    [[noreturn]] void err(int line, int col, const std::string& msg) const {
        throw ScriptError(line, col, msg);
    }

    [[noreturn]] void errHere(const std::string& msg) const { err(cur().line, cur().col, msg); }

    Token expect(Tok k, const char* what) {
        if (!at(k)) {
            errHere(std::string("ожидалось ") + what + ", а найдено '" + cur().text + "'");
        }
        return advance();
    }

    void expectSemi() { expect(Tok::Semicolon, "';'"); }

    // --------------------------------------------------------------- symbols

    Slot declareVar(const std::string& name, int line, int col) {
        auto& top = scopes_.back();
        if (top.count(name)) err(line, col, "переменная '" + name + "' уже объявлена в этой области");
        Slot s;
        if (inFunc_) {
            s.index = nextLocal_++;
            s.global = false;
        } else {
            s.index = nextGlobal_++;
            s.global = true;
            globals_[name] = s.index;
        }
        top[name] = s;
        return s;
    }

    bool lookupVar(const std::string& name, Slot& out) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) {
                out = f->second;
                return true;
            }
        }
        auto g = globals_.find(name);
        if (g != globals_.end()) {
            out.index = g->second;
            out.global = true;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------- statements

    StmtPtr makeStmt(SK k, const Token& t) {
        auto s = std::make_unique<Stmt>();
        s->k = k;
        s->line = t.line;
        s->col = t.col;
        return s;
    }

    StmtPtr parseStatement() {
        const Token t = cur();

        if (t.kind == Tok::LBrace) return parseBlock();
        if (t.kind == Tok::Semicolon) {
            advance();
            return makeStmt(SK::Empty, t);
        }

        if (t.kind == Tok::Ident) {
            const std::string& kw = t.text;
            if (kw == "if") return parseIf();
            if (kw == "while") return parseWhile();
            if (kw == "for") return parseFor();
            if (kw == "return") return parseReturn();
            if (kw == "break" || kw == "continue") return parseLoopJump(kw);
            if (kw == "func") return parseFuncDecl();
            if (isTypeKeyword(kw)) {
                if (peek(1).kind == Tok::Ident && peek(2).kind == Tok::LParen) return parseFuncDecl();
                return parseDeclStatement(true);
            }
        }

        ExprPtr e = parseExpr();
        expectSemi();
        StmtPtr s = makeStmt(SK::Expr, t);
        s->e = std::move(e);
        return s;
    }

    StmtPtr parseBlock() {
        Token open = cur();
        expect(Tok::LBrace, "'{'");
        scopes_.emplace_back();
        StmtPtr s = makeStmt(SK::Block, open);
        while (!at(Tok::RBrace)) {
            if (at(Tok::End)) err(open.line, open.col, "не закрыта '{'");
            s->list.push_back(parseStatement());
        }
        advance();
        scopes_.pop_back();
        return s;
    }

    StmtPtr parseIf() {
        Token t = advance();
        expect(Tok::LParen, "'(' после 'if'");
        ExprPtr cond = parseExpr();
        expect(Tok::RParen, "')'");
        StmtPtr s = makeStmt(SK::If, t);
        s->e = std::move(cond);
        s->s1 = parseStatement();
        if (atIdent("else")) {
            advance();
            s->s2 = parseStatement();
        }
        return s;
    }

    StmtPtr parseWhile() {
        Token t = advance();
        expect(Tok::LParen, "'(' после 'while'");
        ExprPtr cond = parseExpr();
        expect(Tok::RParen, "')'");
        StmtPtr s = makeStmt(SK::While, t);
        s->e = std::move(cond);
        ++loopDepth_;
        s->s1 = parseStatement();
        --loopDepth_;
        return s;
    }

    StmtPtr parseFor() {
        Token t = advance();
        expect(Tok::LParen, "'(' после 'for'");
        scopes_.emplace_back();
        StmtPtr s = makeStmt(SK::For, t);

        if (!at(Tok::Semicolon)) {
            if (cur().kind == Tok::Ident && isTypeKeyword(cur().text) && peek(1).kind == Tok::Ident) {
                s->s1 = parseDeclStatement(true);
            } else {
                Token et = cur();
                ExprPtr e = parseExpr();
                expectSemi();
                StmtPtr es = makeStmt(SK::Expr, et);
                es->e = std::move(e);
                s->s1 = std::move(es);
            }
        } else {
            advance();
        }

        if (!at(Tok::Semicolon)) s->e = parseExpr();
        expect(Tok::Semicolon, "';'");
        if (!at(Tok::RParen)) s->e2 = parseExpr();
        expect(Tok::RParen, "')'");

        ++loopDepth_;
        s->s2 = parseStatement();
        --loopDepth_;
        scopes_.pop_back();
        return s;
    }

    StmtPtr parseLoopJump(const std::string& kw) {
        Token t = advance();
        expectSemi();
        if (loopDepth_ == 0) {
            err(t.line, t.col, kw == "break" ? "'break' вне цикла" : "'continue' вне цикла");
        }
        return makeStmt(kw == "break" ? SK::Break : SK::Continue, t);
    }

    StmtPtr parseReturn() {
        Token t = advance();
        if (funcDepth_ == 0) err(t.line, t.col, "'return' вне функции");
        StmtPtr s = makeStmt(SK::Return, t);
        if (!at(Tok::Semicolon)) s->e = parseExpr();
        expectSemi();
        return s;
    }

    StmtPtr parseDeclStatement(bool consumeSemi) {
        Token typeTok = advance();  // type keyword
        (void)typeTok;
        StmtPtr block = makeStmt(SK::Block, cur());
        for (;;) {
            Token nameTok = expect(Tok::Ident, "имя переменной");
            Slot slot = declareVar(nameTok.text, nameTok.line, nameTok.col);
            ExprPtr init;
            if (match(Tok::Assign)) init = parseExpr();
            StmtPtr d = makeStmt(SK::Decl, nameTok);
            d->slot = slot.index;
            d->globalSlot = slot.global;
            d->e = std::move(init);
            block->list.push_back(std::move(d));
            if (!match(Tok::Comma)) break;
        }
        if (consumeSemi) expectSemi();
        if (block->list.size() == 1) return std::move(block->list[0]);
        return block;
    }

    StmtPtr parseFuncDecl() {
        Token first = advance();  // 'func' or return type
        (void)first;
        Token nameTok = expect(Tok::Ident, "имя функции");
        if (funcIds_.count(nameTok.text)) {
            err(nameTok.line, nameTok.col, "функция '" + nameTok.text + "' уже определена");
        }
        expect(Tok::LParen, "'(' после имени функции");

        std::vector<std::unordered_map<std::string, Slot>> savedScopes = std::move(scopes_);
        int savedNextLocal = nextLocal_;
        bool savedInFunc = inFunc_;
        int savedLoop = loopDepth_;
        int savedFuncDepth = funcDepth_;

        scopes_.clear();
        scopes_.emplace_back();
        nextLocal_ = 0;
        inFunc_ = true;
        loopDepth_ = 0;
        funcDepth_ = 1;

        FuncDecl fd;
        fd.name = nameTok.text;
        fd.line = nameTok.line;
        fd.col = nameTok.col;

        if (!at(Tok::RParen)) {
            for (;;) {
                if (cur().kind == Tok::Ident && isTypeKeyword(cur().text) && peek(1).kind == Tok::Ident) {
                    advance();
                }
                Token p = expect(Tok::Ident, "имя параметра");
                if (scopes_.back().count(p.text)) {
                    err(p.line, p.col, "параметр '" + p.text + "' повторяется");
                }
                Slot ps;
                ps.index = nextLocal_++;
                ps.global = false;
                scopes_.back()[p.text] = ps;
                fd.params.push_back(p.text);
                fd.paramSlots.push_back(ps.index);
                if (!match(Tok::Comma)) break;
            }
        }
        expect(Tok::RParen, "')'");

        fd.body = parseBlock();
        fd.maxSlots = nextLocal_;

        scopes_ = std::move(savedScopes);
        nextLocal_ = savedNextLocal;
        inFunc_ = savedInFunc;
        loopDepth_ = savedLoop;
        funcDepth_ = savedFuncDepth;

        int id = (int)prog_.funcs.size();
        prog_.funcs.push_back(std::move(fd));
        funcIds_[nameTok.text] = id;

        StmtPtr s = makeStmt(SK::FuncDecl, nameTok);
        s->funcId = id;
        return s;
    }

    // ------------------------------------------------------------ expressions

    ExprPtr parseExpr() { return parseAssign(); }

    ExprPtr parseAssign() {
        ExprPtr left = parseBinary(1);
        Tok k = cur().kind;
        Op op = Op::None;
        bool isAssign = false;
        switch (k) {
            case Tok::Assign: isAssign = true; op = Op::None; break;
            case Tok::PlusAssign: isAssign = true; op = Op::Add; break;
            case Tok::MinusAssign: isAssign = true; op = Op::Sub; break;
            case Tok::StarAssign: isAssign = true; op = Op::Mul; break;
            case Tok::SlashAssign: isAssign = true; op = Op::Div; break;
            case Tok::PercentAssign: isAssign = true; op = Op::Mod; break;
            default: break;
        }
        if (!isAssign) return left;

        Token t = advance();
        if (left->k != EK::Var) {
            err(t.line, t.col, "слева от присваивания должна быть переменная");
        }
        ExprPtr right = parseAssign();
        auto e = std::make_unique<Expr>();
        e->k = EK::Assign;
        e->op = op;
        e->line = t.line;
        e->col = t.col;
        e->a = std::move(left);
        e->b = std::move(right);
        return e;
    }

    static bool binOp(Tok k, Op& op, int& prec) {
        switch (k) {
            case Tok::OrOr: op = Op::Or; prec = 1; return true;
            case Tok::AndAnd: op = Op::And; prec = 2; return true;
            case Tok::Pipe: op = Op::BOr; prec = 3; return true;
            case Tok::Caret: op = Op::BXor; prec = 4; return true;
            case Tok::Amp: op = Op::BAnd; prec = 5; return true;
            case Tok::Eq: op = Op::Eq; prec = 6; return true;
            case Tok::Ne: op = Op::Ne; prec = 6; return true;
            case Tok::Lt: op = Op::Lt; prec = 7; return true;
            case Tok::Le: op = Op::Le; prec = 7; return true;
            case Tok::Gt: op = Op::Gt; prec = 7; return true;
            case Tok::Ge: op = Op::Ge; prec = 7; return true;
            case Tok::Shl: op = Op::Shl; prec = 8; return true;
            case Tok::Shr: op = Op::Shr; prec = 8; return true;
            case Tok::Plus: op = Op::Add; prec = 9; return true;
            case Tok::Minus: op = Op::Sub; prec = 9; return true;
            case Tok::Star: op = Op::Mul; prec = 10; return true;
            case Tok::Slash: op = Op::Div; prec = 10; return true;
            case Tok::Percent: op = Op::Mod; prec = 10; return true;
            default: return false;
        }
    }

    ExprPtr binary(Op op, ExprPtr a, ExprPtr b, const Token& t) {
        auto e = std::make_unique<Expr>();
        e->k = EK::Binary;
        e->op = op;
        e->line = t.line;
        e->col = t.col;
        e->a = std::move(a);
        e->b = std::move(b);
        return e;
    }

    ExprPtr parseBinary(int minPrec) {
        ExprPtr left = parseUnary();
        for (;;) {
            Op op;
            int prec = 0;
            if (!binOp(cur().kind, op, prec)) break;
            if (prec < minPrec) break;
            Token t = advance();
            ExprPtr right = parseBinary(prec + 1);
            left = binary(op, std::move(left), std::move(right), t);
        }
        return left;
    }

    ExprPtr unary(Op op, ExprPtr operand, const Token& t) {
        auto e = std::make_unique<Expr>();
        e->k = EK::Unary;
        e->op = op;
        e->line = t.line;
        e->col = t.col;
        e->a = std::move(operand);
        return e;
    }

    ExprPtr parseUnary() {
        Token t = cur();
        switch (t.kind) {
            case Tok::Not: {
                advance();
                return unary(Op::Not, parseUnary(), t);
            }
            case Tok::Minus: {
                advance();
                return unary(Op::Neg, parseUnary(), t);
            }
            case Tok::Plus: {
                advance();
                return unary(Op::Plus, parseUnary(), t);
            }
            case Tok::Tilde: {
                advance();
                return unary(Op::BNot, parseUnary(), t);
            }
            case Tok::PlusPlus:
            case Tok::MinusMinus: {
                advance();
                ExprPtr operand = parseUnary();
                if (operand->k != EK::Var) {
                    err(t.line, t.col, "оператор ++/-- применим только к переменной");
                }
                return unary(t.kind == Tok::PlusPlus ? Op::PreInc : Op::PreDec, std::move(operand), t);
            }
            default: break;
        }
        return parsePostfix();
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        for (;;) {
            if (at(Tok::PlusPlus) || at(Tok::MinusMinus)) {
                Token t = advance();
                if (e->k != EK::Var) {
                    err(t.line, t.col, "оператор ++/-- применим только к переменной");
                }
                e = unary(t.kind == Tok::PlusPlus ? Op::PostInc : Op::PostDec, std::move(e), t);
                continue;
            }
            if (at(Tok::LParen)) errHere("вызов возможен только по имени функции");
            if (at(Tok::LBracket)) errHere("индексация не поддерживается");
            break;
        }
        return e;
    }

    ExprPtr parsePrimary() {
        Token t = cur();
        switch (t.kind) {
            case Tok::Number: {
                advance();
                auto e = std::make_unique<Expr>();
                e->line = t.line;
                e->col = t.col;
                if (t.isFloat) {
                    e->k = EK::FloatLit;
                    e->dv = t.dval;
                } else {
                    e->k = EK::IntLit;
                    e->iv = t.ival;
                }
                return e;
            }
            case Tok::String: {
                advance();
                auto e = std::make_unique<Expr>();
                e->k = EK::StrLit;
                e->line = t.line;
                e->col = t.col;
                e->sv = t.text;
                return e;
            }
            case Tok::LParen: {
                advance();
                ExprPtr e = parseExpr();
                expect(Tok::RParen, "')'");
                return e;
            }
            case Tok::Ident: {
                advance();
                if (t.text == "true" || t.text == "false") {
                    auto e = std::make_unique<Expr>();
                    e->k = EK::BoolLit;
                    e->line = t.line;
                    e->col = t.col;
                    e->iv = (t.text == "true") ? 1 : 0;
                    return e;
                }
                if (t.text == "null") {
                    auto e = std::make_unique<Expr>();
                    e->k = EK::NullLit;
                    e->line = t.line;
                    e->col = t.col;
                    return e;
                }
                if (at(Tok::LParen)) {
                    advance();
                    auto e = std::make_unique<Expr>();
                    e->k = EK::Call;
                    e->sv = t.text;
                    e->line = t.line;
                    e->col = t.col;
                    if (!at(Tok::RParen)) {
                        for (;;) {
                            e->args.push_back(parseExpr());
                            if (!match(Tok::Comma)) break;
                        }
                    }
                    expect(Tok::RParen, "')' в вызове функции");
                    pending_.push_back({e.get(), t.text, t.line, t.col});
                    return e;
                }
                auto e = std::make_unique<Expr>();
                e->k = EK::Var;
                e->name = t.text;
                e->line = t.line;
                e->col = t.col;
                Slot slot;
                if (!lookupVar(t.text, slot)) {
                    err(t.line, t.col, "неизвестная переменная '" + t.text + "'");
                }
                e->slot = slot.index;
                e->globalSlot = slot.global;
                return e;
            }
            default:
                err(t.line, t.col, "ожидалось выражение, а найдено '" + t.text + "'");
        }
    }
};

}  // namespace

Program parse(const std::vector<Token>& tokens, const std::string& path) {
    Parser p(tokens, path);
    return p.parse();
}
