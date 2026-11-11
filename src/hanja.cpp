// rmpp-lang: Hangul -> Hanja conversion (add-ons with convert=hanja, e.g. ko).
//
// Trigger: the "漢" button on the keyboard's format bar, or the Hanja / F9 key of a physical keyboard. The word
// before the cursor (or the selection) is looked up; a candidate bar on the format bar lists the Hanja for the
// longest matching run of syllables first, then shorter ones down to the last syllable. Picking one replaces those
// syllables with an input-method commit (the same kind of replace-before-cursor commit the Hangul engine uses).
// While the bar is open, 1-9 pick, Left/Right/Space page, Esc closes.
//
// Dictionary: the add-on's convert_dict (languages/ko/dict/hanja.txt, built by tools/hanja-dict.py from libhangul,
// BSD-3-Clause): `reading:hanja:meaning` lines sorted by the reading's UTF-8 bytes, memory-mapped and
// binary-searched, so nothing is loaded into xochitl's memory.

#include "lang.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
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

namespace {

constexpr int kMaxRun = 8;        // syllables looked at before the cursor
constexpr int kMaxCands = 300;    // candidates shown in total

bool isSyllable(QChar c) { return c.unicode() >= 0xAC00 && c.unicode() <= 0xD7A3; }

// ---- dictionary ----

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

    // Entries for one reading, in file order: (hanja, meaning).
    QList<QPair<QString, QString>> lookup(const QString &reading) const {
        QList<QPair<QString, QString>> out;
        if (!m_data) return out;
        const QByteArray key = reading.toUtf8();
        qint64 lo = m_start, hi = m_size;
        while (lo < hi) {                       // first line whose reading is >= key
            qint64 ls = (lo + hi) / 2;
            while (ls > lo && m_data[ls - 1] != '\n') --ls;
            if (compare(ls, key) < 0) lo = lineEnd(ls) + 1;
            else hi = ls;
        }
        for (qint64 p = lo; p < m_size && compare(p, key) == 0; p = lineEnd(p) + 1) {
            const qint64 e = lineEnd(p);
            const QString line = QString::fromUtf8(reinterpret_cast<const char *>(m_data + p), int(e - p));
            const QStringList f = line.split(QLatin1Char(':'));
            if (f.size() >= 2) out.append({f.at(1), f.mid(2).join(QLatin1Char(':'))});
        }
        return out;
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
    // The line's reading (up to ':') against key, as bytes.
    int compare(qint64 p, const QByteArray &key) const {
        const qint64 e = lineEnd(p);
        const void *colon = std::memchr(m_data + p, ':', size_t(e - p));
        const qint64 n = colon ? static_cast<const uchar *>(colon) - (m_data + p) : e - p;
        const int c = std::memcmp(m_data + p, key.constData(), size_t(qMin<qint64>(n, key.size())));
        if (c) return c;
        return n < key.size() ? -1 : n > key.size() ? 1 : 0;
    }
};

Dict g_dict;

// The dictionary of the current input language ("" = it converts nothing).
QString currentDict() {
    const QString code = rmpplang::currentInput();
    if (rmpplang::convertKind(code) != QLatin1String("hanja")) return {};
    return rmpplang::convertDict(code);
}

// ---- state shared with the QML on the format bar ----

QQmlPropertyMap *g_map = nullptr;   // available, open, cands (JSON), page, perPage; command written by QML
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

// Look up the selection, or the syllables before the cursor, and open the bar.
bool convert() {
    const QString path = currentDict();
    if (path.isEmpty() || !g_dict.open(path)) return false;
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
        for (const auto &e : g_dict.lookup(word)) cands.append({e.first, e.second, -1});
        g_selLen = sel.size();
        g_selFrom = (anchor < cur) ? -sel.size() : 0;
    } else {
        if (cur <= 0 || cur > text.size()) {
            qWarning("[hanja] no text before the cursor (field gives %d chars, cursor %d)", int(text.size()), cur);
            return false;
        }
        int n = 0;
        while (n < kMaxRun && cur - n - 1 >= 0 && isSyllable(text.at(cur - n - 1))) ++n;
        for (int len = n; len >= 1 && cands.size() < kMaxCands; --len)
            for (const auto &e : g_dict.lookup(text.mid(cur - len, len))) {
                cands.append({e.first, e.second, len});
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

int perPage() { return qMax(1, g_map->value(QStringLiteral("perPage")).toInt()); }

void turnPage(int delta) {
    const int pages = (int(g_cands.size()) + perPage() - 1) / perPage();
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
            const int i = g_map->value(QStringLiteral("page")).toInt() * perPage() + (key - Qt::Key_1);
            if (key - Qt::Key_1 < perPage()) pick(i);
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
    readonly property int per: Math.max(3, Math.floor((width - 320) / 150))
    onPerChanged: hanja.perPage = per
    Component.onCompleted: hanja.perPage = per

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
            text: "漢"
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
                model: root.cands.slice(hanja.page * root.per, (hanja.page + 1) * root.per)
                Rectangle {
                    width: 144; height: 64; radius: 8; color: "white"
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
                        onClicked: hanja.command = "pick:" + (hanja.page * root.per + index) + "#" + Date.now()
                    }
                }
            }
            Rectangle {
                width: 56; height: 64; radius: 8; color: "black"; border.color: "white"; border.width: 2
                opacity: (hanja.page + 1) * root.per < root.cands.length ? 1 : 0.3
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
    g_map->insert(QStringLiteral("open"), false);
    g_map->insert(QStringLiteral("cands"), QStringLiteral("[]"));
    g_map->insert(QStringLiteral("page"), 0);
    g_map->insert(QStringLiteral("perPage"), 6);
    g_map->insert(QStringLiteral("command"), QString());
    QObject::connect(g_map, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key == QLatin1String("command")) onCommand(v.toString());
    });
    auto *timer = new QTimer(filter);
    QObject::connect(timer, &QTimer::timeout, filter, [] {
        const bool avail = !currentDict().isEmpty() && QFileInfo::exists(currentDict());
        if (g_map->value(QStringLiteral("available")).toBool() != avail) {
            g_map->insert(QStringLiteral("available"), avail);
            if (!avail) closeBar();
        }
        syncBar();
    });
    timer->start(1500);
    qWarning("[hanja] ready");
}
