// rmpp-lang: conversion from Hangul, with one candidate bar for every kind of add-on:
//   convert=hanja   (ko) Hangul -> Hanja, the dictionary is keyed by Hangul.
//   convert=kana    (ja) Japanese typed in Hangul: syllables -> kana by the add-on's reading table (convert_map),
//                   then kana -> words (SKK dictionary, okurigana endings added by the core).
//   convert=pinyin  (zh) Chinese typed in Hangul as Koreans spell Mandarin: one or two syllables -> a pinyin
//                   syllable by the reading table, then pinyin -> words ranked by frequency.
//
// Trigger: the button on the keyboard's format bar (漢 / 変 / 汉), or the Hanja / F9 key of a physical keyboard.
// The selection, or the Hangul syllables before the cursor, are looked up: candidates for the longest run first,
// then shorter ones down to the last syllable. Picking one replaces those syllables with an input-method commit
// (the same kind of replace-before-cursor commit the Hangul engine uses). While the bar is open, 1-9 pick,
// Left/Right/Space page, Esc closes.
//
// Dictionaries (the add-on's convert_dict, built by tools/hanja-dict.py, ja-dict.py, zh-dict.py): lines
// `reading:word:note` or `reading<TAB>word<TAB>note[<TAB>weight]`, sorted by the reading's UTF-8 bytes,
// memory-mapped and binary-searched, so they are not loaded into xochitl's memory. Reading tables are small and
// read into memory.

#include "lang.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlPropertyMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cstring>
#include <functional>

namespace {

constexpr int kMaxRun = 10;       // syllables looked at before the cursor
constexpr int kMaxCands = 300;    // candidates shown in total
constexpr int kMaxReadings = 48;  // readings tried per run (kana / pinyin)
constexpr int kMaxNodes = 4000;   // search steps per run

bool isSyllable(QChar c) { return c.unicode() >= 0xAC00 && c.unicode() <= 0xD7A3; }

// ---- dictionary ----

struct Entry { QString word, note; qint64 weight = 0; };

class Dict {
public:
    bool open(const QString &path) {
        const QFileInfo fi(path);
        if (path == m_path && fi.lastModified() == m_stamp) return m_data;
        close();
        m_path = path; m_stamp = fi.lastModified();
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::ReadOnly)) return false;
        m_size = m_file.size();
        m_data = m_file.map(0, m_size);
        if (!m_data) { m_file.close(); return false; }
        // Skip the license header (# lines) at the top.
        m_start = 0;
        while (m_start < m_size && m_data[m_start] == '#') m_start = lineEnd(m_start) + 1;
        qWarning("[hanja] dictionary %s (%lld bytes)", qPrintable(path), m_size);
        return true;
    }
    void close() {
        if (m_data) m_file.unmap(const_cast<uchar *>(m_data));
        m_file.close();
        m_data = nullptr; m_size = m_start = 0; m_path.clear();
    }

    // Entries for one reading, in file order.
    QList<Entry> lookup(const QString &reading) const {
        QList<Entry> out;
        if (!m_data) return out;
        const QByteArray key = reading.toUtf8();
        for (qint64 p = lowerBound(key); p < m_size && compare(p, key) == 0; p = lineEnd(p) + 1) {
            const qint64 e = lineEnd(p);
            const QString line = QString::fromUtf8(reinterpret_cast<const char *>(m_data + p), int(e - p));
            if (line.contains(QLatin1Char('\t'))) {
                const QStringList f = line.split(QLatin1Char('\t'));
                if (f.size() >= 2) out.append({f.at(1), f.value(2), f.value(3).toLongLong()});
            } else {
                const QStringList f = line.split(QLatin1Char(':'));
                if (f.size() >= 2) out.append({f.at(1), f.mid(2).join(QLatin1Char(':')), 0});
            }
        }
        return out;
    }
    // Does any reading start with prefix?
    bool hasPrefix(const QString &prefix) const {
        if (!m_data) return false;
        const QByteArray key = prefix.toUtf8();
        const qint64 p = lowerBound(key);
        return p < m_size && m_size - p >= key.size()
            && std::memcmp(m_data + p, key.constData(), size_t(key.size())) == 0;
    }

private:
    QFile m_file;
    QString m_path;
    QDateTime m_stamp;
    const uchar *m_data = nullptr;
    qint64 m_size = 0, m_start = 0;

    qint64 lineEnd(qint64 p) const {
        const void *nl = std::memchr(m_data + p, '\n', size_t(m_size - p));
        return nl ? static_cast<const uchar *>(nl) - m_data : m_size;
    }
    qint64 lowerBound(const QByteArray &key) const {   // first line whose reading is >= key
        qint64 lo = m_start, hi = m_size;
        while (lo < hi) {
            qint64 ls = (lo + hi) / 2;
            while (ls > lo && m_data[ls - 1] != '\n') --ls;
            if (compare(ls, key) < 0) lo = lineEnd(ls) + 1;
            else hi = ls;
        }
        return lo;
    }
    // The line's reading (up to ':' or tab) against key, as bytes.
    int compare(qint64 p, const QByteArray &key) const {
        const qint64 e = lineEnd(p);
        qint64 n = p;
        while (n < e && m_data[n] != ':' && m_data[n] != '\t') ++n;
        n -= p;
        const int c = std::memcmp(m_data + p, key.constData(), size_t(qMin<qint64>(n, key.size())));
        if (c) return c;
        return n < key.size() ? -1 : n > key.size() ? 1 : 0;
    }
};

Dict g_dict;

// ---- reading tables (Hangul -> kana / pinyin) ----

struct ReadingMap {
    QString path;
    QDateTime stamp;
    QHash<QString, QStringList> map;   // one or two Hangul syllables -> readings, strict first
    int maxChunk = 1;
    bool load(const QString &p) {
        const QFileInfo fi(p);
        if (p == path && fi.lastModified() == stamp) return !map.isEmpty();
        path = p; stamp = fi.lastModified(); map.clear(); maxChunk = 1;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return false;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const QString key = line.section(QLatin1Char('\t'), 0, 0);
            map.insert(key, line.section(QLatin1Char('\t'), 1).split(QLatin1Char(' '), Qt::SkipEmptyParts));
            if (!key.startsWith(QLatin1Char('^'))) maxChunk = qMax(maxChunk, int(key.size()));
        }
        qWarning("[hanja] reading table %s (%d entries)", qPrintable(p), int(map.size()));
        return !map.isEmpty();
    }
};
ReadingMap g_readings;

// SKK's okurigana key letter for the first kana of a word ending (書く: かk).
QChar okuriLetter(QChar k) {
    static const QHash<QChar, QChar> m = [] {
        QHash<QChar, QChar> h;
        const struct { const char16_t *kana; char letter; } rows[] = {
            {u"あ", 'a'}, {u"い", 'i'}, {u"う", 'u'}, {u"え", 'e'}, {u"お", 'o'}, {u"かきくけこ", 'k'},
            {u"がぎぐげご", 'g'}, {u"さしすせそ", 's'}, {u"ざじずぜぞ", 'z'}, {u"たちつてとっ", 't'},
            {u"だぢづでど", 'd'}, {u"なにぬねの", 'n'}, {u"はひふへほ", 'h'}, {u"ばびぶべぼ", 'b'},
            {u"ぱぴぷぺぽ", 'p'}, {u"まみむめも", 'm'}, {u"やゆよ", 'y'}, {u"らりるれろ", 'r'}, {u"わを", 'w'},
            {u"ん", 'n'}};
        for (const auto &r : rows)
            for (const char16_t *c = r.kana; *c; ++c) h.insert(QChar(*c), QChar::fromLatin1(r.letter));
        return h;
    }();
    return m.value(k);
}

// Kana word with an okurigana ending: stem key + ending (かk + く -> 書く).
QList<Entry> okuriLookup(const QString &kana) {
    QList<Entry> out;
    for (int j = int(kana.size()) - 1; j >= qMax(1, int(kana.size()) - 4); --j) {   // endings of up to 4 kana
        const QChar l = okuriLetter(kana.at(j));
        if (l.isNull()) continue;
        for (Entry e : g_dict.lookup(kana.left(j) + l)) {
            e.word += kana.mid(j);
            out.append(e);
        }
    }
    return out;
}
bool okuriStemExists(const QString &kana) {
    for (int j = int(kana.size()) - 1; j >= 1; --j)
        if (const QChar l = okuriLetter(kana.at(j)); !l.isNull() && !g_dict.lookup(kana.left(j) + l).isEmpty())
            return true;
    return false;
}

// Readings of a Hangul run that the dictionary knows (for kana also those ending in an okurigana word).
QStringList readingsOf(const QString &run, bool kana) {
    QStringList out;
    int nodes = 0;
    std::function<void(int, const QString &, bool)> walk = [&](int pos, const QString &built, bool ending) {
        if (out.size() >= kMaxReadings || ++nodes > kMaxNodes) return;
        if (pos == run.size()) { if (!out.contains(built)) out.append(built); return; }
        for (int len = qMin(g_readings.maxChunk, int(run.size()) - pos); len >= 1; --len) {
            // "^syllable": the order of readings at the start of a word (voiceless kana first for ja)
            auto it = pos == 0 ? g_readings.map.constFind(QLatin1Char('^') + run.mid(pos, len)) : g_readings.map.constEnd();
            if (it == g_readings.map.constEnd()) it = g_readings.map.constFind(run.mid(pos, len));
            if (it == g_readings.map.constEnd()) continue;
            for (int a = 0; a < it->size(); ++a) {
                if (ending && a > 0) break;          // inside an okurigana ending: only the strict kana
                const QString next = built + it->at(a);
                if (ending || g_dict.hasPrefix(next)) walk(pos + len, next, ending);
                else if (kana && okuriStemExists(next)) walk(pos + len, next, true);
            }
        }
    };
    walk(0, QString(), false);
    return out;
}

// The strict reading of a run (first alternative of each syllable), "" if a syllable has none.
QString strictReading(const QString &run) {
    QString out;
    for (int pos = 0; pos < run.size();) {
        bool found = false;
        for (int len = qMin(g_readings.maxChunk, int(run.size()) - pos); len >= 1 && !found; --len) {
            const auto it = g_readings.map.constFind(run.mid(pos, len));
            if (it != g_readings.map.constEnd() && !it->isEmpty()) { out += it->first(); pos += len; found = true; }
        }
        if (!found) return {};
    }
    return out;
}

QString toKatakana(const QString &s) {
    QString out = s;
    for (QChar &c : out)
        if (c.unicode() >= 0x3041 && c.unicode() <= 0x3096) c = QChar(char16_t(c.unicode() + 0x60));
    return out;
}

// The conversion of the current input language: kind ("" = none), dictionary and reading table.
struct Conv { QString kind, dict, map; };
Conv currentConv() {
    const QString code = rmpplang::currentInput();
    Conv c{rmpplang::convertKind(code), rmpplang::convertDict(code), rmpplang::convertMap(code)};
    if (c.kind == QLatin1String("hanja")) return c;
    if ((c.kind == QLatin1String("kana") || c.kind == QLatin1String("pinyin")) && !c.map.isEmpty()) return c;
    return {};
}
QString currentDict() { return currentConv().dict; }

QString buttonLabel(const QString &kind) {
    if (kind == QLatin1String("kana")) return QStringLiteral("変");
    if (kind == QLatin1String("pinyin")) return QStringLiteral("汉");
    return QStringLiteral("漢");
}

// ---- state shared with the QML on the format bar ----

QQmlPropertyMap *g_map = nullptr;   // available, label, open, cands (JSON), page, pageStart/Size/Total; command written by QML
QPointer<QObject> g_target;         // the text field the candidates are for
struct Cand { QString hanja, meaning; int len; };
QList<Cand> g_cands;
int g_selFrom = 0, g_selLen = 0;    // replacement relative to the cursor, for a selection

void closeBar() {
    g_cands.clear();
    g_target = nullptr;
    if (g_map && g_map->value(QStringLiteral("open")).toBool()) {
        g_map->insert(QStringLiteral("open"), false);
        g_map->insert(QStringLiteral("cands"), QStringLiteral("[]"));
    }
}

// Candidates for one Hangul run (all of it is replaced).
QList<Entry> candidatesFor(const Conv &conv, const QString &run, bool withKana) {
    if (conv.kind == QLatin1String("hanja")) return g_dict.lookup(run);
    const bool kana = conv.kind == QLatin1String("kana");
    QList<Entry> out;
    QSet<QString> seen;
    const auto add = [&](const Entry &e) {
        if (!seen.contains(e.word)) { seen.insert(e.word); out.append(e); }
    };
    const QStringList rs = readingsOf(run, kana);
    if (!kana) {   // pinyin: every reading's words, most frequent first
        for (const QString &r : rs)
            for (const Entry &e : g_dict.lookup(r)) add(e);
        std::stable_sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.weight > b.weight; });
        return out;
    }
    // kana: SKK has no frequencies, and Korean spelling leaves voicing open at the start of a word (지칸 = じかん,
    // 간지 = かんじ): take the readings' words in turns (first of each, then second of each, ...), then the kana of
    // the readings that matched, then okurigana forms, then plain hiragana / katakana.
    QList<QList<Entry>> lists;
    QStringList matched;
    for (const QString &r : rs) {
        lists.append(g_dict.lookup(r));
        if (!lists.last().isEmpty() && matched.size() < 3) matched.append(r);
    }
    for (int i = 0, more = 1; more; ++i) {
        more = 0;
        for (const auto &l : std::as_const(lists))
            if (i < l.size()) { add(l.at(i)); more = 1; }
    }
    for (const QString &r : std::as_const(matched)) add({r, {}, 0});
    for (const QString &r : rs)
        for (const Entry &e : okuriLookup(r)) add(e);
    if (withKana) {
        if (const QString s = strictReading(run); !s.isEmpty()) { add({s, {}, 0}); add({toKatakana(s), {}, 0}); }
    }
    return out;
}

// Look up the selection, or the syllables before the cursor, and open the bar.
bool convert() {
    const Conv conv = currentConv();
    if (conv.kind.isEmpty() || !g_dict.open(conv.dict)) return false;
    if (!conv.map.isEmpty() && !g_readings.load(conv.map)) return false;
    QObject *obj = QGuiApplication::focusObject();
    if (!obj) return false;
    QInputMethodQueryEvent q(Qt::ImSurroundingText | Qt::ImCursorPosition | Qt::ImAnchorPosition
                             | Qt::ImCurrentSelection);
    QCoreApplication::sendEvent(obj, &q);
    const QString text = q.value(Qt::ImSurroundingText).toString();
    const QString sel = q.value(Qt::ImCurrentSelection).toString();
    const int cur = q.value(Qt::ImCursorPosition).isValid() ? q.value(Qt::ImCursorPosition).toInt() : -1;
    const int anchor = q.value(Qt::ImAnchorPosition).isValid() ? q.value(Qt::ImAnchorPosition).toInt() : cur;

    QList<Cand> cands;
    g_selFrom = g_selLen = 0;
    if (!sel.isEmpty()) {
        // A selected word: replace exactly the selection.
        const QString word = sel.trimmed();
        if (word.size() > kMaxRun || !std::all_of(word.begin(), word.end(), isSyllable)) return false;
        for (const Entry &e : candidatesFor(conv, word, true)) cands.append({e.word, e.note, -1});
        g_selLen = int(sel.size());
        g_selFrom = (anchor < cur) ? -int(sel.size()) : 0;
    } else {
        if (cur <= 0 || cur > text.size()) {
            qWarning("[hanja] no text before the cursor (field gives %d chars, cursor %d)", int(text.size()), cur);
            return false;
        }
        int n = 0;
        while (n < kMaxRun && cur - n - 1 >= 0 && isSyllable(text.at(cur - n - 1))) ++n;
        for (int len = n; len >= 1 && cands.size() < kMaxCands; --len)
            for (const Entry &e : candidatesFor(conv, text.mid(cur - len, len), len == n)) {
                cands.append({e.word, e.note, len});
                if (cands.size() >= kMaxCands) break;
            }
    }
    if (cands.isEmpty()) { closeBar(); return false; }
    g_cands = cands;
    g_target = obj;
    QJsonArray arr;
    for (const Cand &c : cands) arr.append(QJsonObject{{"h", c.hanja}, {"m", c.meaning}});
    g_map->insert(QStringLiteral("cands"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    g_map->insert(QStringLiteral("page"), 0);
    g_map->insert(QStringLiteral("open"), true);
    return true;
}

void pick(int i) {
    if (i < 0 || i >= g_cands.size()) return;
    const Cand c = g_cands.at(i);
    QObject *obj = g_target ? g_target.data() : QGuiApplication::focusObject();
    closeBar();
    if (!obj) return;
    QInputMethodEvent im;
    if (c.len < 0) im.setCommitString(c.hanja, g_selFrom, g_selLen);
    else im.setCommitString(c.hanja, -c.len, c.len);
    QCoreApplication::sendEvent(obj, &im);
}

// The QML lays candidates out by their width and reports the current page: first index, size, number of pages.
int pageStart() { return g_map->value(QStringLiteral("pageStart")).toInt(); }
int pageSize() { return qMax(1, g_map->value(QStringLiteral("pageSize")).toInt()); }

void turnPage(int delta) {
    const int pages = qMax(1, g_map->value(QStringLiteral("pageTotal")).toInt());
    const int p = g_map->value(QStringLiteral("page")).toInt() + delta;
    if (p >= 0 && p < pages) g_map->insert(QStringLiteral("page"), p);
}

// ---- physical keys ----

class HanjaFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (ev->type() != QEvent::KeyPress || obj != QGuiApplication::focusObject()) return false;
        auto *k = static_cast<QKeyEvent *>(ev);
        if (k->key() == Qt::Key_Hangul_Hanja || (k->key() == Qt::Key_F9 && k->modifiers() == Qt::NoModifier)) {
            if (currentDict().isEmpty()) return false;
            if (!k->isAutoRepeat()) convert();
            return true;
        }
        if (g_cands.isEmpty()) return false;
        const int key = k->key();
        if (key >= Qt::Key_1 && key <= Qt::Key_9 && k->modifiers() == Qt::NoModifier) {
            if (key - Qt::Key_1 < pageSize()) pick(pageStart() + (key - Qt::Key_1));
            return true;
        }
        if (key == Qt::Key_Right || key == Qt::Key_Space || key == Qt::Key_Tab) { turnPage(1); return true; }
        if (key == Qt::Key_Left || key == Qt::Key_Backtab) { turnPage(-1); return true; }
        if (key == Qt::Key_Escape) { closeBar(); return true; }
        if (key != Qt::Key_Shift) closeBar();   // anything else: type it, the bar goes away
        return false;
    }
};

// ---- format bar ----

// "漢" button left of the language indicator, and the candidate bar over the rest of the format bar.
const char *kHanjaQml = R"QML(
import QtQuick
Item {
    id: root
    anchors.fill: parent
    z: 6
    readonly property var cands: hanja.open ? JSON.parse(hanja.cands || "[]") : []
    // Cells as wide as their word (食べます, 中国人), pages filled greedily to the room on the bar.
    FontMetrics { id: fm; font.pixelSize: 32 }
    function cellWidth(c) { return Math.min(420, Math.max(144, Math.ceil(fm.advanceWidth(c.h)) + 32)); }
    readonly property real room: Math.max(300, width - 200 - 76 - 12 - 16 - 2 * (56 + 6))
    readonly property var pages: {
        const out = [0];
        let used = 0;
        for (let i = 0; i < cands.length; ++i) {
            const w = cellWidth(cands[i]) + 6;
            if (used > 0 && (used + w > room || i - out[out.length - 1] >= 9)) { out.push(i); used = 0; }
            used += w;
        }
        return out;
    }
    readonly property int first: pages[Math.min(hanja.page, pages.length - 1)] || 0
    readonly property int last: hanja.page + 1 < pages.length ? pages[hanja.page + 1] : cands.length
    function report() { hanja.pageStart = first; hanja.pageSize = last - first; hanja.pageTotal = pages.length; }
    onFirstChanged: report()
    onLastChanged: report()
    onPagesChanged: report()

    Rectangle {
        id: button
        width: 76; height: 56; radius: 10
        anchors.right: parent.right; anchors.rightMargin: 200
        anchors.verticalCenter: parent.verticalCenter
        visible: !!hanja.available
        color: hanja.open ? "white" : "black"
        border.color: "white"; border.width: 2
        Text {
            anchors.centerIn: parent
            text: hanja.label || "漢"
            color: hanja.open ? "black" : "white"
            // Regular weight: bold 漢 at this size filled in on e-paper (developer: "thinner font")
            font.family: "Noto Sans KR"; font.weight: Font.Normal; font.pixelSize: 34
        }
        MouseArea {
            anchors.fill: parent; anchors.margins: -8
            onClicked: hanja.command = (hanja.open ? "close#" : "convert#") + Date.now()
        }
    }

    Rectangle {
        visible: !!hanja.open
        anchors.left: parent.left; anchors.right: button.left; anchors.rightMargin: 12
        anchors.top: parent.top; anchors.bottom: parent.bottom
        color: "black"
        MouseArea { anchors.fill: parent }   // keep taps off the bar's own buttons underneath
        Row {
            anchors.left: parent.left; anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle {
                width: 56; height: 64; radius: 8; color: "black"; border.color: "white"; border.width: 2
                opacity: hanja.page > 0 ? 1 : 0.3
                Text { anchors.centerIn: parent; text: "‹"; color: "white"; font.pixelSize: 40 }
                MouseArea { anchors.fill: parent; onClicked: hanja.command = "page:-1#" + Date.now() }
            }
            Repeater {
                model: root.cands.slice(root.first, root.last)
                Rectangle {
                    width: root.cellWidth(modelData); height: 64; radius: 8; color: "white"
                    Text {
                        x: 6; y: 2
                        text: String(index + 1)
                        font.pixelSize: 16; color: "black"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 2; width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.h
                        font.pixelSize: 32; color: "black"
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.m || ""
                        font.pixelSize: 16; color: "black"
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: hanja.command = "pick:" + (root.first + index) + "#" + Date.now()
                    }
                }
            }
            Rectangle {
                width: 56; height: 64; radius: 8; color: "black"; border.color: "white"; border.width: 2
                opacity: hanja.page + 1 < root.pages.length ? 1 : 0.3
                Text { anchors.centerIn: parent; text: "›"; color: "white"; font.pixelSize: 40 }
                MouseArea { anchors.fill: parent; onClicked: hanja.command = "page:1#" + Date.now() }
            }
        }
    }
}
)QML";

QQuickItem *findByClassPrefix(QQuickItem *root, const char *prefix, int depth = 0) {
    if (!root || depth > 60) return nullptr;
    if (qstrncmp(root->metaObject()->className(), prefix, qstrlen(prefix)) == 0) return root;
    for (QQuickItem *c : root->childItems())
        if (QQuickItem *f = findByClassPrefix(c, prefix, depth + 1)) return f;
    return nullptr;
}

void syncBar() {
    for (QWindow *w : QGuiApplication::allWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw) continue;
        QQuickItem *bar = findByClassPrefix(qw->contentItem(), "FormatMenuVirtualKeyboard");
        if (!bar || bar->property("_hanjaBar").toBool()) continue;
        bar->setProperty("_hanjaBar", true);
        QQmlEngine *eng = qmlEngine(bar);
        if (!eng) continue;
        auto *ctx = new QQmlContext(eng->rootContext(), bar);
        ctx->setContextProperty(QStringLiteral("hanja"), g_map);
        QQmlComponent comp(eng);
        comp.setData(QByteArray(kHanjaQml), QUrl(QStringLiteral("qrc:/hanja/HanjaBar.qml")));
        QObject *o = comp.beginCreate(ctx);
        auto *item = qobject_cast<QQuickItem *>(o);
        if (!item) {
            for (const auto &e : comp.errors()) qWarning("[hanja] %s", qPrintable(e.toString()));
            delete o;
            continue;
        }
        item->setParentItem(bar);
        item->setParent(bar);
        comp.completeCreate();
        qWarning("[hanja] button added to the format bar");
    }
}

void onCommand(const QString &cmd) {
    const QString verb = cmd.section(QLatin1Char('#'), 0, 0);
    if (verb == QLatin1String("convert")) convert();
    else if (verb == QLatin1String("close")) closeBar();
    else if (verb.startsWith(QLatin1String("pick:"))) pick(verb.mid(5).toInt());
    else if (verb.startsWith(QLatin1String("page:"))) turnPage(verb.mid(5).toInt());
}

} // namespace

void rmpplang::hanjaInit() {
    auto *app = QCoreApplication::instance();
    auto *filter = new HanjaFilter(app);
    app->installEventFilter(filter);
    g_map = new QQmlPropertyMap(app);
    g_map->insert(QStringLiteral("available"), false);
    g_map->insert(QStringLiteral("label"), QStringLiteral("漢"));
    g_map->insert(QStringLiteral("open"), false);
    g_map->insert(QStringLiteral("cands"), QStringLiteral("[]"));
    g_map->insert(QStringLiteral("page"), 0);
    g_map->insert(QStringLiteral("pageStart"), 0);
    g_map->insert(QStringLiteral("pageSize"), 6);
    g_map->insert(QStringLiteral("pageTotal"), 1);
    g_map->insert(QStringLiteral("command"), QString());
    QObject::connect(g_map, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key == QLatin1String("command")) onCommand(v.toString());
    });
    auto *timer = new QTimer(filter);
    QObject::connect(timer, &QTimer::timeout, filter, [] {
        const Conv conv = currentConv();
        const bool avail = !conv.dict.isEmpty() && QFileInfo::exists(conv.dict);
        if (g_map->value(QStringLiteral("label")).toString() != buttonLabel(conv.kind))
            g_map->insert(QStringLiteral("label"), buttonLabel(conv.kind));
        if (g_map->value(QStringLiteral("available")).toBool() != avail) {
            g_map->insert(QStringLiteral("available"), avail);
            if (!avail) closeBar();
        }
        syncBar();
    });
    timer->start(1500);
    qWarning("[hanja] ready");
}
