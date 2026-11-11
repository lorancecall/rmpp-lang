// rmpp-lang Korean keyboard: Hangul (2-beolsik) composition for the on-screen keyboard and physical keyboards.
// Part of librmpp-lang.so (loaded into xochitl with LD_PRELOAD); active while the input language's add-on says
// input=compose:hangul (languages/ko). Its on-screen layout ko_KR comes from the add-on's ko.rcc. This file
// rewrites single-jamo commits into composed syllables.
#include "lang.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QObject>
#include <QtCore/QResource>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QInputMethod>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QMetaMethod>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlPropertyMap>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <functional>
#include <vector>

namespace {

const char16_t CHO[]  = u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";        // 19
const char16_t JUNG[] = u"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";   // 21
const char16_t JONG[] = u" ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"; // 28, [0] = none

int indexIn(const char16_t *tab, int n, char16_t c) {
    for (int i = 0; i < n; ++i) if (tab[i] == c) return i;
    return -1;
}

// Old Hangul (옛한글), typed from the format bar's palette with the Korean "old" input method: archaic letters
// as compatibility jamo (initial, final) or conjoining initials (clusters, Mandarin sibilants), archaic vowels.
// A syllable with any of them is written as conjoining jamo (U+1100 L, U+1161 V, U+11A8 T), which fonts with
// the ljmo / vjmo / tjmo features (the OS's Noto Sans KR) draw as one syllable; modern syllables stay precomposed.
struct OldConsonant { char16_t compat, initial, final; };
const OldConsonant OLD_CONSONANTS[] = {
    {u'ㅿ', 0x1140, 0x11EB}, {u'ㆆ', 0x1159, 0x11F9}, {u'ㆁ', 0x114C, 0x11F0}, {u'ㅸ', 0x112B, 0x11E6},
    {u'ㅱ', 0x111D, 0x11E2}, {u'ㆄ', 0x1157, 0x11F4}, {u'ㆅ', 0x1158, 0}, {u'ㅹ', 0x112C, 0},
};
const OldConsonant *oldConsonant(char16_t c) {
    for (const auto &o : OLD_CONSONANTS) if (o.compat == c) return &o;
    return nullptr;
}
bool isConjoiningInitial(char16_t c) { return (c >= 0x1100 && c <= 0x115E) || (c >= 0xA960 && c <= 0xA97C); }
bool isOldVowel(char16_t c) { return c == u'ㆍ' || c == u'ㆎ' || c == 0x11A2; }   // ㆍ, ㆎ, ᆢ

bool isVowel(char16_t c) { return (c >= 0x314F && c <= 0x3163) || isOldVowel(c); }
bool isConsonant(char16_t c) { return (c >= 0x3131 && c <= 0x314E) || oldConsonant(c) || isConjoiningInitial(c); }
bool isJamo(char16_t c) { return isVowel(c) || isConsonant(c); }

bool validInitial(char16_t c) { return indexIn(CHO, 19, c) >= 0 || oldConsonant(c) || isConjoiningInitial(c); }
bool validFinal(char16_t c) {
    if (indexIn(JONG, 28, c) > 0) return true;
    const OldConsonant *o = oldConsonant(c);
    return o && o->final;
}

char16_t combineVowel(char16_t a, char16_t b) {
    static const char16_t t[][3] = {{u'ㅗ',u'ㅏ',u'ㅘ'},{u'ㅗ',u'ㅐ',u'ㅙ'},{u'ㅗ',u'ㅣ',u'ㅚ'},
        {u'ㅜ',u'ㅓ',u'ㅝ'},{u'ㅜ',u'ㅔ',u'ㅞ'},{u'ㅜ',u'ㅣ',u'ㅟ'},{u'ㅡ',u'ㅣ',u'ㅢ'},
        {u'ㆍ',u'ㅣ',u'ㆎ'},{u'ㆍ',u'ㆍ',char16_t(0x11A2)}};
    for (auto &r : t) if (r[0] == a && r[1] == b) return r[2];
    return 0;
}
char16_t combineFinal(char16_t a, char16_t b) {
    static const char16_t t[][3] = {{u'ㄱ',u'ㅅ',u'ㄳ'},{u'ㄴ',u'ㅈ',u'ㄵ'},{u'ㄴ',u'ㅎ',u'ㄶ'},
        {u'ㄹ',u'ㄱ',u'ㄺ'},{u'ㄹ',u'ㅁ',u'ㄻ'},{u'ㄹ',u'ㅂ',u'ㄼ'},{u'ㄹ',u'ㅅ',u'ㄽ'},
        {u'ㄹ',u'ㅌ',u'ㄾ'},{u'ㄹ',u'ㅍ',u'ㄿ'},{u'ㄹ',u'ㅎ',u'ㅀ'},{u'ㅂ',u'ㅅ',u'ㅄ'},
        {u'ㄹ',u'ㆆ',char16_t(0x11D9)},{u'ㄹ',u'ㅿ',char16_t(0x11D7)}};   // old: ㄹㆆ, ㄹㅿ
    for (auto &r : t) if (r[0] == a && r[1] == b) return r[2];
    return 0;
}

struct Parsed { bool ok = false; char16_t cho = 0, jung = 0, jong = 0; };

// Parse a sequence of typed jamo as one syllable block.
Parsed parse(const std::vector<char16_t> &s) {
    Parsed p;
    size_t i = 0, n = s.size();
    if (n == 0) return p;
    if (isConsonant(s[0])) {
        if (!validInitial(s[0])) return p;
        p.cho = s[0]; i = 1;
    }
    if (i < n && isVowel(s[i])) {
        p.jung = s[i++];
        if (i < n && isVowel(s[i])) {
            char16_t v = combineVowel(p.jung, s[i]);
            if (v) { p.jung = v; ++i; }
        }
    }
    if (!p.cho && !p.jung) return p;
    if (i < n) {
        if (!p.cho || !p.jung || !isConsonant(s[i]) || !(validFinal(s[i]) || s[i] >= 0x11A8)) return p;
        p.jong = s[i++];
        if (i < n && isConsonant(s[i])) {
            char16_t f = combineFinal(p.jong, s[i]);
            if (f) { p.jong = f; ++i; }
        }
    }
    p.ok = (i == n);
    return p;
}

// Conjoining forms for an old syllable (0 = none).
char16_t initialOf(char16_t c) {
    if (int i = indexIn(CHO, 19, c); i >= 0) return char16_t(0x1100 + i);
    if (const OldConsonant *o = oldConsonant(c)) return o->initial;
    return isConjoiningInitial(c) ? c : 0;
}
char16_t vowelOf(char16_t c) {
    if (int i = indexIn(JUNG, 21, c); i >= 0) return char16_t(0x1161 + i);
    if (c == u'ㆍ') return 0x119E;
    if (c == u'ㆎ') return 0x11A1;
    return c >= 0x1160 && c <= 0x11A7 ? c : 0;
}
char16_t finalOf(char16_t c) {
    if (int i = indexIn(JONG, 28, c); i > 0) return char16_t(0x11A8 + i - 1);
    if (const OldConsonant *o = oldConsonant(c)) return o->final;
    return c >= 0x11A8 && c <= 0x11FF ? c : 0;
}

QString render(const Parsed &p) {
    if (!p.ok) return {};
    const bool modern = (!p.cho || indexIn(CHO, 19, p.cho) >= 0) && (!p.jung || indexIn(JUNG, 21, p.jung) >= 0)
                        && (!p.jong || indexIn(JONG, 28, p.jong) > 0);
    if (modern) {
        if (p.cho && p.jung) {
            int c = indexIn(CHO, 19, p.cho), v = indexIn(JUNG, 21, p.jung);
            int f = p.jong ? indexIn(JONG, 28, p.jong) : 0;
            return QString(QChar(char16_t(0xAC00 + (c * 21 + v) * 28 + f)));
        }
        return QString(QChar(p.cho ? p.cho : p.jung));
    }
    if (!p.cho || !p.jung) {                  // a lone old letter
        const char16_t c = p.cho ? p.cho : p.jung;
        return QString(QChar(c == 0x11A2 ? char16_t(0x11A2) : c));
    }
    QString out;
    out += QChar(initialOf(p.cho));
    out += QChar(vowelOf(p.jung));
    if (p.jong) out += QChar(finalOf(p.jong));
    return out;
}

// Input language for physical keyboards, shared with the on-screen keyboard and the indicator.
bool g_korean = false;              // the input language composes Hangul
bool composesHangul() { return rmpplang::inputMethod(rmpplang::currentInput()) == QLatin1String("compose:hangul"); }
void applyInput(const char *why);
void noteJamo(char16_t j);          // a jamo was typed (swipe.cpp part below)

class HangulFilter : public QObject {
public:
    using QObject::QObject;

    // A swipe on a key: the jamo it just typed becomes an old letter (ㅈ -> ᅎ). Called after the commit was applied.
    void replaceLast(char16_t from, char16_t to) {
        QObject *obj = QGuiApplication::focusObject();
        if (!obj || block.empty() || block.back() != from || !stillComposing(obj)) return;
        const int prevLen = int(lastOut.size());
        std::vector<char16_t> grown = block;
        grown.back() = to;
        const Parsed p = parse(grown);
        QString out;
        if (p.ok) {
            block = grown;
            lastOut = render(p);
            out = lastOut;
        } else {                       // e.g. 맛 + swipe on ㅅ -> 마 + ᄼ (an old initial cannot be a final)
            std::vector<char16_t> prev(block.begin(), block.end() - 1);
            const Parsed pp = parse(prev);
            if (prev.empty() || !pp.ok) return;
            block.assign(1, to);
            lastOut = render(parse(block));
            out = render(pp) + lastOut;
        }
        QInputMethodEvent im;
        im.setCommitString(out, -prevLen, prevLen);
        ownSend = true;
        QCoreApplication::sendEvent(obj, &im);
        ownSend = false;
        owner = obj;
        lastCursor = cursorOf(obj);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        switch (ev->type()) {
        case QEvent::InputMethod:
            return onInputMethod(obj, static_cast<QInputMethodEvent *>(ev));
        case QEvent::KeyPress:
            return onKeyPress(obj, static_cast<QKeyEvent *>(ev));
        case QEvent::ShortcutOverride:
            if (debug) qWarning("[hangul] shortcutOverride key=0x%x obj=%s", static_cast<QKeyEvent *>(ev)->key(), obj->metaObject()->className());
            break;
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            reset();
            break;
        default:
            break;
        }
        return false;
    }

private:
    std::vector<char16_t> block;   // jamo typed into the current syllable
    QString lastOut;               // the character we last left before the cursor
    QObject *owner = nullptr;      // the text field we composed in
    int lastCursor = -1;
    QEvent *lastEvent = nullptr;   // event we already rewrote
    QString lastSet;               // commit string we wrote into it
    bool ownSend = false;

    void reset() { block.clear(); lastOut.clear(); owner = nullptr; lastCursor = -1; }

    // Is the character before the cursor still the one we produced?
    bool stillComposing(QObject *obj) {
        if (block.empty() || obj != owner) return false;
        QInputMethodQueryEvent q(Qt::ImSurroundingText | Qt::ImCursorPosition);
        QCoreApplication::sendEvent(obj, &q);
        QString text = q.value(Qt::ImSurroundingText).toString();
        QVariant cv = q.value(Qt::ImCursorPosition);
        if (text.isEmpty() || !cv.isValid()) return true;  // field gives no info: trust our state
        int cur = cv.toInt();
        if (cur <= 0 || cur > text.size()) return false;
        if (lastCursor >= 0 && cur != lastCursor) return false;
        return cur >= lastOut.size() && text.mid(cur - lastOut.size(), lastOut.size()) == lastOut;
    }

    int cursorOf(QObject *obj) {
        QInputMethodQueryEvent q(Qt::ImCursorPosition);
        QCoreApplication::sendEvent(obj, &q);
        QVariant v = q.value(Qt::ImCursorPosition);
        return v.isValid() ? v.toInt() : -1;
    }

    bool onInputMethod(QObject *obj, QInputMethodEvent *e) {
        if (ownSend) return false;
        if (e == lastEvent && e->commitString() == lastSet) return false;  // re-delivered
        if (obj != QGuiApplication::focusObject()) return false;
        QString commit = e->commitString();
        const bool plain = e->preeditString().isEmpty() && e->replacementLength() == 0;

        // Type Folio keys can reach the text field as input-method commits (xochitl's own input context)
        // rather than key events. In Korean mode, map a single Latin letter to its 2-beolsik jamo here too.
        if (g_korean && plain && commit.size() == 1) {
            const char16_t c = commit.at(0).unicode();
            char16_t j = 0;
            if (c >= u'a' && c <= u'z') j = jamoForKey(Qt::Key_A + (c - u'a'), false);
            else if (c >= u'A' && c <= u'Z') j = jamoForKey(Qt::Key_A + (c - u'A'), true);
            if (j) {
                commit = QString(QChar(j));
                e->setCommitString(commit);
            }
        }

        if (!(plain && commit.size() == 1 && isJamo(commit.at(0).unicode()))) {
            // Empty commit replacing the previous char acts as a backspace.
            if (commit.isEmpty() && e->replacementStart() == -1 && e->replacementLength() == 1
                && stillComposing(obj) && block.size() > 1) {
                const int prev = int(lastOut.size());
                block.pop_back();
                lastOut = render(parse(block));
                e->setCommitString(lastOut, -prev, prev);
                lastEvent = e; lastSet = lastOut;
                return false;
            }
            if (!commit.isEmpty() || e->replacementLength() != 0) reset();
            return false;
        }

        const char16_t j = commit.at(0).unicode();
        noteJamo(j);
        if (!stillComposing(obj)) block.clear();

        if (!block.empty()) {
            std::vector<char16_t> grown = block;
            grown.push_back(j);
            Parsed p = parse(grown);
            const int prevLen = int(lastOut.size());
            if (p.ok) {
                block = grown;
                lastOut = render(p);
                e->setCommitString(lastOut, -prevLen, prevLen);
                finish(obj, int(lastOut.size()) - prevLen);
                lastEvent = e; lastSet = lastOut;
                return false;
            }
            // A vowel after a final consonant: the consonant moves to a new syllable.
            Parsed cur = parse(block);
            if (isVowel(j) && cur.ok && cur.jong) {
                std::vector<char16_t> prev(block.begin(), block.end() - 1);
                std::vector<char16_t> next{block.back(), j};
                Parsed pp = parse(prev), pn = parse(next);
                if (pp.ok && pn.ok) {
                    QString out = render(pp) + render(pn);
                    block = next;
                    lastOut = render(pn);
                    e->setCommitString(out, -prevLen, prevLen);
                    finish(obj, int(out.size()) - prevLen);
                    lastEvent = e; lastSet = out;
                    return false;
                }
            }
        }
        block.assign(1, j);
        lastOut = QString(QChar(j));
        finish(obj, 1);
        return false;
    }

    // Remember where the cursor will be after the event is applied.
    void finish(QObject *obj, int delta) {
        owner = obj;
        lastEvent = nullptr; lastSet.clear();
        int c = cursorOf(obj);
        lastCursor = (c >= 0) ? c + delta : -1;
    }

    // ---- physical keyboard (Type Folio / Bluetooth) ----
    ulong lastToggleTs = ~0UL;
    bool debug = qEnvironmentVariableIntValue("HANGUL_DEBUG") != 0;

    static bool isToggleKey(const QKeyEvent *k) {
        const quint32 sc = k->nativeScanCode();
        return k->key() == Qt::Key_Hangul || k->key() == Qt::Key_AltGr
            || sc == 100 || sc == 108      // KEY_RIGHTALT (evdev / +8)
            || sc == 126 || sc == 134;     // KEY_RIGHTMETA (evdev / +8)
    }

    static char16_t jamoForKey(int key, bool shift) {
        static const char16_t base[26] = {   // a..z on 2-beolsik
            u'ㅁ',u'ㅠ',u'ㅊ',u'ㅇ',u'ㄷ',u'ㄹ',u'ㅎ',u'ㅗ',u'ㅑ',u'ㅓ',u'ㅏ',u'ㅣ',u'ㅡ',
            u'ㅜ',u'ㅐ',u'ㅔ',u'ㅂ',u'ㄱ',u'ㄴ',u'ㅅ',u'ㅕ',u'ㅍ',u'ㅈ',u'ㅌ',u'ㅛ',u'ㅋ'};
        if (key < Qt::Key_A || key > Qt::Key_Z) return 0;
        char16_t j = base[key - Qt::Key_A];
        if (shift) {
            switch (j) {
            case u'ㅂ': return u'ㅃ'; case u'ㅈ': return u'ㅉ'; case u'ㄷ': return u'ㄸ';
            case u'ㄱ': return u'ㄲ'; case u'ㅅ': return u'ㅆ'; case u'ㅐ': return u'ㅒ';
            case u'ㅔ': return u'ㅖ'; default: break;
            }
        }
        return j;
    }

    bool onKeyPress(QObject *obj, QKeyEvent *k) {
        const bool atFocus = (obj == QGuiApplication::focusObject());
        if (debug)
            qWarning("[hangul] key=0x%x sc=%u mods=0x%x text=%s obj=%s focus=%d", k->key(), k->nativeScanCode(),
                     uint(k->modifiers()), qPrintable(k->text()), obj->metaObject()->className(), int(atFocus));

        // Ctrl+Space: toggle, and swallow it so no space is typed.
        if (k->key() == Qt::Key_Space && (k->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier) {
            if (rmpplang::inputCycle().size() < 2) return false;
            if (!k->isAutoRepeat() && k->timestamp() != lastToggleTs) {
                lastToggleTs = k->timestamp();
                rmpplang::cycleInput();
                applyInput("key");
                reset();
            }
            return true;
        }
        if (isToggleKey(k)) {
            if (rmpplang::inputCycle().size() < 2) return false;
            if (atFocus && !k->isAutoRepeat()) {
                rmpplang::cycleInput();
                applyInput("key");
                reset();
            }
            return false;
        }
        if (!atFocus) return false;

        if (k->key() == Qt::Key_Backspace && k->modifiers() == Qt::NoModifier)
            return onBackspace(obj);

        const auto mods = k->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        if (g_korean && mods == Qt::NoModifier) {
            if (char16_t j = jamoForKey(k->key(), k->modifiers() & Qt::ShiftModifier)) {
                QInputMethodEvent im;
                im.setCommitString(QString(QChar(j)));
                QCoreApplication::sendEvent(obj, &im);   // composed by onInputMethod
                return true;                             // swallow the Latin key
            }
        }
        if (k->key() != Qt::Key_Shift) reset();
        return false;
    }

    bool onBackspace(QObject *obj) {
        if (block.size() <= 1 || !stillComposing(obj)) {
            reset();
            return false;
        }
        const int prev = int(lastOut.size());
        block.pop_back();
        lastOut = render(parse(block));
        QInputMethodEvent im;
        im.setCommitString(lastOut, -prev, prev);
        ownSend = true;
        QCoreApplication::sendEvent(obj, &im);
        ownSend = false;
        owner = obj;
        lastCursor = cursorOf(obj);
        return true;   // swallow the real backspace
    }
};

// External (Bluetooth bus 0005 / USB bus 0003) keyboards now attached, by their MAC/serial ("U: Uniq=").
QStringList externalKeyboards() {
    QFile f(QStringLiteral("/proc/bus/input/devices"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    bool ext = false;
    QString uniq;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("I:")) { ext = line.contains("Bus=0005") || line.contains("Bus=0003"); uniq.clear(); }
        else if (line.startsWith("U: Uniq=")) uniq = QString::fromLatin1(line.mid(8)).trimmed().toUpper();
        else if (line.startsWith("H:") && ext && line.contains("kbd")) out << (uniq.isEmpty() ? QStringLiteral("?") : uniq);
    }
    return out;
}

bool externalKeyboardPresent() { return !externalKeyboards().isEmpty(); }

// Keyboards set to "Type Folio mode" in the Bluetooth page (rmpp-bt writes this file: one "ADDRESS=folio" per line).
bool anyFolioKeyboard(const QStringList &present) {
    QFile f(QStringLiteral("/home/root/.config/rmpp-bt/keyboards.conf"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> kv = line.trimmed().split('=');
        if (kv.size() == 2 && kv[1] == "folio" && present.contains(QString::fromLatin1(kv[0]).toUpper())) return true;
    }
    return false;
}

// Visual-tree search for an item that carries a "keyboardInfo" property (VirtualKeyboardWindow.qml).
QObject *keyboardInfoInTree(QQuickItem *it, int depth = 0) {
    if (!it || depth > 200) return nullptr;
    QVariant v = it->property("keyboardInfo");
    if (QObject *o = v.value<QObject *>()) return o;
    const auto kids = it->childItems();
    for (QQuickItem *k : kids)
        if (QObject *o = keyboardInfoInTree(k, depth + 1)) return o;
    return nullptr;
}

QObject *findKeyboardInfo() {
    const auto windows = QGuiApplication::allWindows();
    for (QWindow *w : windows) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw) continue;
        // "Use global KeyboardInfo": a context property on the engine's root context.
        const auto kids = qw->contentItem()->childItems();
        for (QQuickItem *k : kids) {
            if (QQmlEngine *eng = qmlEngine(k)) {
                QVariant v = eng->rootContext()->contextProperty(QStringLiteral("KeyboardInfo"));
                if (QObject *o = v.value<QObject *>()) return o;
            }
        }
        if (QObject *o = keyboardInfoInTree(qw->contentItem())) return o;
    }
    return nullptr;
}

QQuickItem *findByClassPrefix(QQuickItem *root, const char *prefix, int depth = 0) {
    if (!root || depth > 200) return nullptr;
    if (qstrncmp(root->metaObject()->className(), prefix, qstrlen(prefix)) == 0) return root;
    const auto kids = root->childItems();
    for (QQuickItem *k : kids)
        if (QQuickItem *f = findByClassPrefix(k, prefix, depth + 1)) return f;
    return nullptr;
}

QQuickItem *findKeyboardPanel() {
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w))
            if (QQuickItem *p = findByClassPrefix(qw->contentItem(), "KeyboardPanel")) return p;
    return nullptr;
}

bool setFakeConnected(QObject *info, bool on) {
    const QMetaObject *mo = info->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        QMetaMethod m = mo->method(i);
        if (m.name() != "setFakeKeyboardConnected") continue;
        return m.parameterCount() == 1 ? m.invoke(info, Qt::DirectConnection, Q_ARG(bool, on))
                                       : (on && m.invoke(info, Qt::DirectConnection));
    }
    return false;
}

// Modes while an external keyboard is attached:
//  keys  (default): fold only the on-screen keys (KeyboardPanel.keyboardConnected); the bottom format bar stays.
//  folio (per keyboard, set in the Bluetooth page): xochitl treats it as a Type Folio (setFakeKeyboardConnected).
void syncExternalKeyboard() {
    enum Mode { None, Keys, Folio };
    static Mode current = None;
    static bool fakeOn = false;
    const QStringList present = externalKeyboards();
    const Mode want = present.isEmpty() ? None : (anyFolioKeyboard(present) ? Folio : Keys);

    QObject *info = findKeyboardInfo();
    if (info) {
        const bool needFake = (want == Folio);
        if (needFake != fakeOn && setFakeConnected(info, needFake)) fakeOn = needFake;
    }
    if (QQuickItem *panel = findKeyboardPanel()) {
        const bool realFolio = info && !fakeOn && info->property("keyboardConnected").toBool();
        const bool fold = (want != None) || realFolio || fakeOn;
        if (panel->property("keyboardConnected").toBool() != fold) panel->setProperty("keyboardConnected", fold);
    }
    if (want != current) {
        qWarning("[hangul] external keyboard: %s", want == None ? "none" : want == Keys ? "keys folded" : "Type Folio mode");
        current = want;
    }
}

// ---- input language: shared by physical keyboards, the on-screen keyboard and the indicator ----

QQmlPropertyMap *g_state = nullptr;      // "korean" (bool) for QML, "command" written by QML
QString g_lastVkLanguage;                // on-screen keyboard language seen last
QString g_latinLanguage = QStringLiteral("en_US");

QObject *findVirtualKeyboard() {
    QQuickItem *panel = findKeyboardPanel();
    return panel ? panel->property("virtualKeyboard").value<QObject *>() : nullptr;
}

// The input language changed (key, indicator, globe key, settings): Korean composition, indicator, on-screen keyboard.
void applyInput(const char *why) {
    const QString cur = rmpplang::currentInput();
    const bool on = composesHangul();
    if (g_korean != on) qWarning("[hangul] Korean input: %s (%s)", on ? "on" : "off", why);
    g_korean = on;
    if (g_state) {
        g_state->insert(QStringLiteral("korean"), on);
        g_state->insert(QStringLiteral("label"), rmpplang::inputLabel());
        g_state->insert(QStringLiteral("enabled"), rmpplang::inputCycle().size() > 1);
        g_state->insert(QStringLiteral("old"), on && rmpplang::oldLetters(cur));
    }
    // Keep the on-screen keyboard on the same language (only if that layout is one of its enabled languages).
    if (qstrcmp(why, "screen") == 0) return;
    if (QObject *vk = findVirtualKeyboard()) {
        const QString vkCur = vk->property("language").toString();
        QString want = rmpplang::screenLayoutFor(cur);
        if (want.isEmpty()) want = g_latinLanguage;
        const QStringList langs = vk->property("languages").toStringList();
        // A layout that is not turned on in Language & keyboard cannot be shown: stay on the Latin layout then
        // (setting it anyway left the on-screen keyboard without keys).
        if (!langs.contains(want)) want = g_latinLanguage;
        if (vkCur != want && langs.contains(want) && vk->setProperty("language", want))
            g_lastVkLanguage = want;
    }
}

// Follow the globe key: when the on-screen language changes, the physical keyboard follows.
void followScreenLanguage() {
    QObject *vk = findVirtualKeyboard();
    if (!vk) return;
    const QString lang = vk->property("language").toString();
    if (lang.isEmpty() || lang == g_lastVkLanguage) return;
    g_lastVkLanguage = lang;
    const QString code = rmpplang::inputForScreenLayout(lang);
    if (code == QLatin1String("en")) g_latinLanguage = lang;
    rmpplang::setInput(code);
    applyInput("screen");
}

// "한" / "EN" indicator on the bottom format bar, left of its close button; tap to switch.
const char *kIndicatorQml = R"QML(
import QtQuick
Rectangle {
    width: 76; height: 56; radius: 10
    color: hangul.label !== "EN" ? "white" : "black"
    border.color: "white"; border.width: 2
    anchors.right: parent.right; anchors.rightMargin: 112
    anchors.verticalCenter: parent.verticalCenter
    z: 5
    visible: !!hangul.enabled
    Text {
        anchors.centerIn: parent
        text: hangul.label || "EN"
        color: hangul.label !== "EN" ? "black" : "white"
        font.pixelSize: 30; font.bold: true
    }
    MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: hangul.command = "toggle#" + Date.now() }
}
)QML";

// Old Hangul palette: a "옛" button left of the conversion button; it opens rows of old letters above the bar.
// A tap sends the letter to the text field like a key, so the Hangul engine composes it (initials, vowels, finals);
// 방점 (U+302E one dot, U+302F two dots) go in as they are, after the syllable.
const char *kOldQml = R"QML(
import QtQuick
Item {
    id: root
    anchors.fill: parent
    z: 7
    visible: !!hangul.old
    property bool open: false
    onVisibleChanged: open = false
    readonly property var rows: [
        ["ㆍ", "ㅿ", "ㆆ", "ㆁ", "ㅸ", "ㅱ", "ㆄ", "ㆅ", "ㅹ"],
        ["\u1122", "\u1123", "\u1127", "\u1129", "\u1120", "\u1121", "\u112D", "\u112F", "\u1132", "\u1136"],
        ["\u113C", "\u113E", "\u114E", "\u1150", "\u1154", "\u1155", "\u302E", "\u302F"]
    ]
    Rectangle {
        width: 76; height: 56; radius: 10
        anchors.right: parent.right; anchors.rightMargin: 288
        anchors.verticalCenter: parent.verticalCenter
        color: root.open ? "white" : "black"
        border.color: "white"; border.width: 2
        Text {
            anchors.centerIn: parent
            text: "옛"
            color: root.open ? "black" : "white"
            font.family: "Noto Sans KR"; font.weight: Font.Normal; font.pixelSize: 34
        }
        MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: root.open = !root.open }
    }
    Rectangle {
        visible: root.open
        anchors.right: parent.right; anchors.rightMargin: 16
        anchors.bottom: parent.top; anchors.bottomMargin: 8
        width: col.width + 24; height: col.height + 24; radius: 12
        color: "black"; border.color: "white"; border.width: 2
        Column {
            id: col
            anchors.centerIn: parent
            spacing: 8
            Repeater {
                model: root.rows
                Row {
                    spacing: 8
                    Repeater {
                        model: modelData
                        Rectangle {
                            width: 84; height: 76; radius: 8; color: "white"
                            Text {
                                anchors.centerIn: parent
                                // the font draws a lone 방점 on its own dotted circle
                                text: modelData
                                color: "black"
                                font.family: "Noto Sans KR"; font.pixelSize: 40
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: hangul.command = "insert:" + modelData + "#" + Date.now()
                            }
                        }
                    }
                }
            }
        }
    }
}
)QML";

void syncOldPalette() {
    for (QWindow *w : QGuiApplication::allWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw) continue;
        QQuickItem *bar = findByClassPrefix(qw->contentItem(), "FormatMenuVirtualKeyboard");
        if (!bar || bar->property("_hangulOld").toBool()) continue;
        bar->setProperty("_hangulOld", true);
        QQmlEngine *eng = qmlEngine(bar);
        if (!eng) continue;
        auto *ctx = new QQmlContext(eng->rootContext(), bar);
        ctx->setContextProperty(QStringLiteral("hangul"), g_state);
        QQmlComponent comp(eng);
        comp.setData(QByteArray(kOldQml), QUrl(QStringLiteral("qrc:/hangul/OldPalette.qml")));
        QObject *o = comp.beginCreate(ctx);
        auto *item = qobject_cast<QQuickItem *>(o);
        if (!item) {
            for (const auto &e : comp.errors()) qWarning("[hangul] %s", qPrintable(e.toString()));
            delete o;
            continue;
        }
        item->setParentItem(bar);
        item->setParent(bar);
        comp.completeCreate();
        qWarning("[hangul] old Hangul palette added to the format bar");
    }
}

// A letter from the palette, typed into the focused field as if from the keyboard.
void insertOld(const QString &s) {
    QObject *obj = QGuiApplication::focusObject();
    if (!obj || s.isEmpty()) return;
    QInputMethodEvent im;
    im.setCommitString(s);
    QCoreApplication::sendEvent(obj, &im);
}

void syncIndicator() {
    for (QWindow *w : QGuiApplication::allWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw) continue;
        QQuickItem *bar = findByClassPrefix(qw->contentItem(), "FormatMenuVirtualKeyboard");
        if (!bar || bar->property("_hangulBadge").toBool()) continue;
        bar->setProperty("_hangulBadge", true);
        QQmlEngine *eng = qmlEngine(bar);
        if (!eng) continue;
        auto *ctx = new QQmlContext(eng->rootContext(), bar);
        ctx->setContextProperty(QStringLiteral("hangul"), g_state);
        QQmlComponent comp(eng);
        comp.setData(QByteArray(kIndicatorQml), QUrl(QStringLiteral("qrc:/hangul/Indicator.qml")));
        QObject *o = comp.beginCreate(ctx);
        auto *item = qobject_cast<QQuickItem *>(o);
        if (!item) {
            for (const auto &e : comp.errors()) qWarning("[hangul] %s", qPrintable(e.toString()));
            delete o;
            continue;
        }
        item->setParentItem(bar);
        item->setParent(bar);
        comp.completeCreate();
        qWarning("[hangul] indicator added to the format bar");
    }
}

// ---- old letters by swiping a key (with old_letters on) ----
// On-screen keys type on press; a swipe that ends more than kSwipe px left / right / up replaces the jamo the key
// just typed: ㅈ ㅊ ㅅ left = 치두음 ᅎ ᅔ ᄼ (z c s), right = 정치음 ᅐ ᅕ ᄾ (zh ch sh) -- the long stroke of the
// letter points that way --, up = the other old letter of the key. Small marks on those keys show the targets.
constexpr qreal kSwipe = 40;
struct SwipeTarget { char16_t key, left, right, up; };
const SwipeTarget SWIPES[] = {
    {u'ㅈ', 0x114E, 0x1150, 0}, {u'ㅊ', 0x1154, 0x1155, 0}, {u'ㅅ', 0x113C, 0x113E, u'ㅿ'},
    {u'ㄹ', 0, 0, u'ㅿ'}, {u'ㅇ', 0, 0, u'ㆁ'}, {u'ㅎ', 0, 0, u'ㆆ'}, {u'ㅂ', 0, 0, u'ㅸ'},
    {u'ㅁ', 0, 0, u'ㅱ'}, {u'ㅍ', 0, 0, u'ㆄ'}, {u'ㅏ', 0, 0, u'ㆍ'},
};
char16_t swipeTarget(char16_t key, char dir) {
    for (const auto &s : SWIPES)
        if (s.key == key) return dir == 'L' ? s.left : dir == 'R' ? s.right : dir == 'U' ? s.up : 0;
    return 0;
}

HangulFilter *g_hangul = nullptr;
char16_t g_lastJamo = 0;
qint64 g_lastJamoMs = 0, g_touchBeginMs = -1, g_pendingUntil = 0;
char g_pendingDir = 0;

bool oldLettersOn() { return composesHangul() && rmpplang::oldLetters(rmpplang::currentInput()); }

void applySwipe(char16_t jamo, char dir) {
    const char16_t to = swipeTarget(jamo, dir);
    if (!to || !g_hangul) return;
    QTimer::singleShot(0, g_hangul, [jamo, to] { g_hangul->replaceLast(jamo, to); });
}

void noteJamo(char16_t j) {
    g_lastJamo = j;
    g_lastJamoMs = QDateTime::currentMSecsSinceEpoch();
    if (g_pendingDir && g_lastJamoMs <= g_pendingUntil) applySwipe(j, g_pendingDir);   // key typed on release
    g_pendingDir = 0;
}

class SwipeFilter : public QObject {
public:
    using QObject::QObject;
protected:
    QPointF start;
    bool eventFilter(QObject *obj, QEvent *ev) override {
        const auto t = ev->type();
        if (t != QEvent::TouchBegin && t != QEvent::TouchEnd) return false;
        auto *w = qobject_cast<QQuickWindow *>(obj);
        if (!w || !oldLettersOn()) return false;
        auto *pe = static_cast<QPointerEvent *>(ev);
        if (pe->points().isEmpty()) return false;
        const QPointF p = pe->points().first().scenePosition();
        if (t == QEvent::TouchBegin) {
            QQuickItem *panel = findKeyboardPanel();
            const bool onKeys = panel && panel->isVisible()
                && panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height())).contains(p);
            g_touchBeginMs = onKeys ? QDateTime::currentMSecsSinceEpoch() : -1;
            start = p;
            return false;
        }
        if (g_touchBeginMs < 0) return false;
        const qreal dx = p.x() - start.x(), dy = p.y() - start.y();
        char dir = 0;
        if (qAbs(dx) >= kSwipe && qAbs(dx) > qAbs(dy)) dir = dx < 0 ? 'L' : 'R';
        else if (-dy >= kSwipe && qAbs(dy) > qAbs(dx)) dir = 'U';
        const qint64 begin = g_touchBeginMs;
        g_touchBeginMs = -1;
        if (!dir) return false;
        if (g_lastJamoMs >= begin) applySwipe(g_lastJamo, dir);      // typed on press (as seen on 3.29)
        else { g_pendingDir = dir; g_pendingUntil = QDateTime::currentMSecsSinceEpoch() + 400; }
        return false;
    }
};

// Marks of the swipe targets on the keys (left / right / top edge), while old letters are on.
const char *kMarkQml = R"QML(
import QtQuick
Item {
    id: m
    property Item key
    anchors.fill: parent
    visible: !!hangul.old
    readonly property var marks: ({
        "ㅈ": ["ᅎ", "ᅐ", ""], "ㅊ": ["ᅔ", "ᅕ", ""], "ㅅ": ["ᄼ", "ᄾ", "ㅿ"],
        "ㄹ": ["", "", "ㅿ"], "ㅇ": ["", "", "ㆁ"], "ㅎ": ["", "", "ㆆ"], "ㅂ": ["", "", "ㅸ"],
        "ㅁ": ["", "", "ㅱ"], "ㅍ": ["", "", "ㆄ"], "ㅏ": ["", "", "ㆍ"] })
    readonly property var mk: marks[key ? key.text : ""] || ["", "", ""]
    readonly property color ink: key && key.color ? key.color : "white"
    Text { anchors.left: parent.left; anchors.leftMargin: 4; anchors.verticalCenter: parent.verticalCenter
           text: m.mk[0]; color: m.ink; font.family: "Noto Sans KR"; font.pixelSize: 20 }
    Text { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
           text: m.mk[1]; color: m.ink; font.family: "Noto Sans KR"; font.pixelSize: 20 }
    Text { anchors.top: parent.top; anchors.topMargin: 2; anchors.horizontalCenter: parent.horizontalCenter
           text: m.mk[2]; color: m.ink; font.family: "Noto Sans KR"; font.pixelSize: 20 }
}
)QML";

void syncSwipeMarks() {
    if (!g_state || !g_state->value(QStringLiteral("old")).toBool()) return;
    QQuickItem *panel = findKeyboardPanel();
    if (!panel) return;
    QQmlEngine *eng = qmlEngine(panel);
    if (!eng) return;
    std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *it, int depth) {
        if (!it || depth > 40) return;
        const QString text = it->property("text").toString();
        if (text.size() == 1 && swipeTarget(text.at(0).unicode(), 'U') + swipeTarget(text.at(0).unicode(), 'L') > 0
            && strstr(it->metaObject()->className(), "Text") && !it->property("_rmppMark").toBool()) {
            it->setProperty("_rmppMark", true);
            QQuickItem *cell = it->parentItem();          // the key: the first ancestor clearly wider than the letter
            while (cell && cell->parentItem() && cell->width() < it->width() + 24) cell = cell->parentItem();
            if (cell) {
                auto *ctx = new QQmlContext(eng->rootContext(), cell);
                ctx->setContextProperty(QStringLiteral("hangul"), g_state);
                QQmlComponent comp(eng);
                comp.setData(QByteArray(kMarkQml), QUrl(QStringLiteral("qrc:/hangul/SwipeMark.qml")));
                QObject *o = comp.beginCreate(ctx);
                if (auto *mark = qobject_cast<QQuickItem *>(o)) {
                    mark->setProperty("key", QVariant::fromValue(it));
                    mark->setParentItem(cell);
                    mark->setParent(cell);
                    comp.completeCreate();
                } else {
                    for (const auto &e : comp.errors()) qWarning("[hangul] %s", qPrintable(e.toString()));
                    delete o;
                }
            }
        }
        for (QQuickItem *c : it->childItems()) walk(c, depth + 1);
    };
    walk(panel, 0);
}

// Research probe (off unless /tmp/rmpp-lang-touch exists): logs touch / mouse events on windows with the
// deepest item under the point, and input-method commits, to learn when on-screen keys commit (press or release)
// and whether swipes or long presses on keys can be seen (old Hangul by sliding a key).
class TouchProbe : public QObject {
public:
    using QObject::QObject;
    bool on = false;
protected:
    static QString itemAt(QQuickWindow *w, QPointF p) {
        QQuickItem *it = w->contentItem(), *found = nullptr;
        for (int depth = 0; it && depth < 80; ++depth) {
            QQuickItem *next = nullptr;
            const auto kids = it->childItems();
            for (auto k = kids.rbegin(); k != kids.rend(); ++k) {
                QQuickItem *c = *k;
                if (c->isVisible() && c->contains(c->mapFromScene(p))) { next = c; break; }
            }
            if (!next) break;
            found = it = next;
        }
        if (!found) return QStringLiteral("-");
        QString t = found->property("text").toString();
        for (QQuickItem *u = found; t.isEmpty() && u; u = u->parentItem()) t = u->property("text").toString();
        return QStringLiteral("%1 \"%2\"").arg(QLatin1String(found->metaObject()->className()), t.left(12));
    }
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (!on) return false;
        const auto t = ev->type();
        if (t == QEvent::InputMethod) {
            auto *e = static_cast<QInputMethodEvent *>(ev);
            qWarning("[touch] commit \"%s\" repl %d,%d to %s", qPrintable(e->commitString()), e->replacementStart(),
                     e->replacementLength(), obj->metaObject()->className());
            return false;
        }
        auto *w = qobject_cast<QQuickWindow *>(obj);
        if (!w) return false;
        if (t == QEvent::TouchBegin || t == QEvent::TouchUpdate || t == QEvent::TouchEnd
            || t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease) {
            auto *pe = static_cast<QPointerEvent *>(ev);
            if (pe->points().isEmpty()) return false;
            const QPointF p = pe->points().first().scenePosition();
            if (t == QEvent::TouchUpdate && pe->points().first().state() == QEventPoint::Stationary) return false;
            qWarning("[touch] %s %.0f,%.0f %s", t == QEvent::TouchBegin ? "begin" : t == QEvent::TouchUpdate ? "move"
                     : t == QEvent::TouchEnd ? "end" : t == QEvent::MouseButtonPress ? "mpress" : "mrelease",
                     p.x(), p.y(), qPrintable(itemAt(w, p)));
        }
        return false;
    }
};

void init() {
    auto *app = QCoreApplication::instance();
    auto *filter = new HangulFilter(app);
    g_hangul = filter;
    app->installEventFilter(filter);
    // Treat a Bluetooth/USB keyboard like the Type Folio: keys hidden, format bar kept.
    g_state = new QQmlPropertyMap(app);
    g_state->insert(QStringLiteral("korean"), false);
    g_state->insert(QStringLiteral("command"), QString());
    g_state->insert(QStringLiteral("enabled"), false);
    g_state->insert(QStringLiteral("label"), QStringLiteral("EN"));
    g_state->insert(QStringLiteral("old"), false);
    QObject::connect(g_state, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key == QLatin1String("command") && v.toString().startsWith(QLatin1String("toggle"))) { rmpplang::cycleInput(); applyInput("indicator"); }
        if (key == QLatin1String("command") && v.toString().startsWith(QLatin1String("insert:")))
            insertOld(v.toString().mid(7).section(QLatin1Char('#'), 0, 0));
    });
    app->installEventFilter(new SwipeFilter(app));
    auto *touch = new TouchProbe(app);
    app->installEventFilter(touch);
    auto *touchTimer = new QTimer(touch);
    QObject::connect(touchTimer, &QTimer::timeout, touch, [touch] {
        touch->on = QFile::exists(QStringLiteral("/tmp/rmpp-lang-touch"));
    });
    touchTimer->start(1000);
    auto *timer = new QTimer(filter);
    QObject::connect(timer, &QTimer::timeout, filter, [] {
        // A language or keyboard turned off in Settings > Languages: currentInput() falls back to English.
        if (g_korean != composesHangul()
            || g_state->value(QStringLiteral("label")).toString() != rmpplang::inputLabel()
            || g_state->value(QStringLiteral("enabled")).toBool() != (rmpplang::inputCycle().size() > 1)
            || g_state->value(QStringLiteral("old")).toBool()
                   != (composesHangul() && rmpplang::oldLetters(rmpplang::currentInput())))
            applyInput("settings");
        syncExternalKeyboard();
        followScreenLanguage();
        syncIndicator();
        syncOldPalette();
        syncSwipeMarks();
    });
    timer->start(1500);
    qWarning("[hangul] filter installed");
}

} // namespace

void rmpplang::hangulInit() { init(); }
