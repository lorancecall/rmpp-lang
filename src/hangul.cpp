// Hangul (2-beolsik) composition for the reMarkable on-screen keyboard.
// Loaded into xochitl with LD_PRELOAD. It registers an extra keyboard layout
// (ko_KR) and rewrites single-jamo commits into composed syllables.
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
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QMetaMethod>
#include <QtGui/QWindow>
#include <vector>

namespace {

const char16_t CHO[]  = u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";        // 19
const char16_t JUNG[] = u"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";   // 21
const char16_t JONG[] = u" ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"; // 28, [0] = none

int indexIn(const char16_t *tab, int n, char16_t c) {
    for (int i = 0; i < n; ++i) if (tab[i] == c) return i;
    return -1;
}
bool isVowel(char16_t c) { return c >= 0x314F && c <= 0x3163; }
bool isConsonant(char16_t c) { return c >= 0x3131 && c <= 0x314E; }
bool isJamo(char16_t c) { return isVowel(c) || isConsonant(c); }

char16_t combineVowel(char16_t a, char16_t b) {
    static const char16_t t[][3] = {{u'ㅗ',u'ㅏ',u'ㅘ'},{u'ㅗ',u'ㅐ',u'ㅙ'},{u'ㅗ',u'ㅣ',u'ㅚ'},
        {u'ㅜ',u'ㅓ',u'ㅝ'},{u'ㅜ',u'ㅔ',u'ㅞ'},{u'ㅜ',u'ㅣ',u'ㅟ'},{u'ㅡ',u'ㅣ',u'ㅢ'}};
    for (auto &r : t) if (r[0] == a && r[1] == b) return r[2];
    return 0;
}
char16_t combineFinal(char16_t a, char16_t b) {
    static const char16_t t[][3] = {{u'ㄱ',u'ㅅ',u'ㄳ'},{u'ㄴ',u'ㅈ',u'ㄵ'},{u'ㄴ',u'ㅎ',u'ㄶ'},
        {u'ㄹ',u'ㄱ',u'ㄺ'},{u'ㄹ',u'ㅁ',u'ㄻ'},{u'ㄹ',u'ㅂ',u'ㄼ'},{u'ㄹ',u'ㅅ',u'ㄽ'},
        {u'ㄹ',u'ㅌ',u'ㄾ'},{u'ㄹ',u'ㅍ',u'ㄿ'},{u'ㄹ',u'ㅎ',u'ㅀ'},{u'ㅂ',u'ㅅ',u'ㅄ'}};
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
        if (indexIn(CHO, 19, s[0]) < 0) return p;
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
        if (!p.cho || !p.jung || !isConsonant(s[i]) || indexIn(JONG, 28, s[i]) <= 0) return p;
        p.jong = s[i++];
        if (i < n && isConsonant(s[i])) {
            char16_t f = combineFinal(p.jong, s[i]);
            if (f) { p.jong = f; ++i; }
        }
    }
    p.ok = (i == n);
    return p;
}

QString render(const Parsed &p) {
    if (!p.ok) return {};
    if (p.cho && p.jung) {
        int c = indexIn(CHO, 19, p.cho), v = indexIn(JUNG, 21, p.jung);
        int f = p.jong ? indexIn(JONG, 28, p.jong) : 0;
        return QString(QChar(char16_t(0xAC00 + (c * 21 + v) * 28 + f)));
    }
    return QString(QChar(p.cho ? p.cho : p.jung));
}

class HangulFilter : public QObject {
public:
    using QObject::QObject;

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
        return text.mid(cur - 1, 1) == lastOut;
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
        const QString commit = e->commitString();
        const bool plain = e->preeditString().isEmpty() && e->replacementLength() == 0;

        if (!(plain && commit.size() == 1 && isJamo(commit.at(0).unicode()))) {
            // Empty commit replacing the previous char acts as a backspace.
            if (commit.isEmpty() && e->replacementStart() == -1 && e->replacementLength() == 1
                && stillComposing(obj) && block.size() > 1) {
                block.pop_back();
                lastOut = render(parse(block));
                e->setCommitString(lastOut, -1, 1);
                lastEvent = e; lastSet = lastOut;
                return false;
            }
            if (!commit.isEmpty() || e->replacementLength() != 0) reset();
            return false;
        }

        const char16_t j = commit.at(0).unicode();
        if (!stillComposing(obj)) block.clear();

        if (!block.empty()) {
            std::vector<char16_t> grown = block;
            grown.push_back(j);
            Parsed p = parse(grown);
            if (p.ok) {
                block = grown;
                lastOut = render(p);
                e->setCommitString(lastOut, -1, 1);
                finish(obj, 0);
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
                    e->setCommitString(out, -1, 1);
                    finish(obj, 1);
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
    bool korean = false;          // physical keyboard mode, toggled by Ctrl+Space or right Alt / Cmd
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
            if (!k->isAutoRepeat() && k->timestamp() != lastToggleTs) {
                lastToggleTs = k->timestamp();
                korean = !korean;
                reset();
                qWarning("[hangul] physical keyboard: %s", korean ? "Korean" : "English");
            }
            return true;
        }
        if (isToggleKey(k)) {
            if (atFocus && !k->isAutoRepeat()) {
                korean = !korean;
                reset();
                qWarning("[hangul] physical keyboard: %s", korean ? "Korean" : "English");
            }
            return false;
        }
        if (!atFocus) return false;

        if (k->key() == Qt::Key_Backspace && k->modifiers() == Qt::NoModifier)
            return onBackspace(obj);

        const auto mods = k->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        if (korean && mods == Qt::NoModifier) {
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
        block.pop_back();
        lastOut = render(parse(block));
        QInputMethodEvent im;
        im.setCommitString(lastOut, -1, 1);
        ownSend = true;
        QCoreApplication::sendEvent(obj, &im);
        ownSend = false;
        owner = obj;
        lastCursor = cursorOf(obj);
        return true;   // swallow the real backspace
    }
};

// True when a Bluetooth or USB keyboard is attached (bus 0005 / 0003 with a kbd handler).
bool externalKeyboardPresent() {
    QFile f(QStringLiteral("/proc/bus/input/devices"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> blocks = f.readAll().split('\n');
    bool ext = false;
    for (const QByteArray &line : blocks) {
        if (line.startsWith("I:")) ext = line.contains("Bus=0005") || line.contains("Bus=0003");
        else if (line.startsWith("H:") && ext && line.contains("kbd")) return true;
    }
    return false;
}

QObject *findKeyboardInfo() {
    const auto windows = QGuiApplication::allWindows();
    for (QWindow *w : windows) {
        QVariant v = w->property("keyboardInfo");
        if (QObject *o = v.value<QObject *>()) return o;
    }
    for (QWindow *w : windows) {
        const auto kids = w->findChildren<QObject *>();
        for (QObject *c : kids) {
            if (QByteArray(c->metaObject()->className()).contains("KeyboardInfo")) return c;
            QVariant v = c->property("keyboardInfo");
            if (QObject *o = v.value<QObject *>()) return o;
        }
    }
    return nullptr;
}

void syncExternalKeyboard() {
    static int last = -1;
    static bool loggedMissing = false;
    const bool present = externalKeyboardPresent();
    if (int(present) == last) return;
    QObject *info = findKeyboardInfo();
    if (!info) {
        if (!loggedMissing) qWarning("[hangul] KeyboardInfo not found yet");
        loggedMissing = true;
        return;
    }
    const QMetaObject *mo = info->metaObject();
    int idx = -1;
    for (int i = 0; i < mo->methodCount(); ++i)
        if (mo->method(i).name() == "setFakeKeyboardConnected") { idx = i; break; }
    if (idx < 0) { qWarning("[hangul] setFakeKeyboardConnected missing on %s", mo->className()); last = present; return; }
    QMetaMethod m = mo->method(idx);
    bool ok = (m.parameterCount() == 1) ? m.invoke(info, Qt::DirectConnection, Q_ARG(bool, present))
                                        : (present ? m.invoke(info, Qt::DirectConnection) : false);
    qWarning("[hangul] external keyboard %s -> %s(%s) %s; keyboardConnected=%s", present ? "present" : "gone",
             m.methodSignature().constData(), present ? "true" : "false", ok ? "ok" : "FAILED",
             info->property("keyboardConnected").toBool() ? "true" : "false");
    last = present;
}

void hangulInit() {
    const char *rcc = qEnvironmentVariableIsSet("HANGUL_RCC") ? nullptr : "/usr/lib/hangul/ko_KR.rcc";
    QString path = rcc ? QString::fromLatin1(rcc) : qEnvironmentVariable("HANGUL_RCC");
    bool ok = QResource::registerResource(path);
    qWarning("[hangul] layout %s: %s", qPrintable(path), ok ? "registered" : "FAILED");
    auto *app = QCoreApplication::instance();
    auto *filter = new HangulFilter(app);
    app->installEventFilter(filter);
    // Treat a Bluetooth/USB keyboard like the Type Folio: keys hidden, format bar kept.
    auto *timer = new QTimer(filter);
    QObject::connect(timer, &QTimer::timeout, filter, [] { syncExternalKeyboard(); });
    timer->start(2000);
    qWarning("[hangul] filter installed");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(hangulInit)
