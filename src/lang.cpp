// rmpp-lang: languages for the reMarkable Paper Pro (xochitl), loaded with LD_PRELOAD via rmpp-preload.
//  - Settings > Languages: languages, per-language keyboard, per-language font.
//  - Fonts per language: files in /home/root/.local/share/fonts/rmpp-lang/<code>/ are registered as application
//    fonts; the chosen family of each language becomes Qt's first fallback for that language's scripts
//    (QFontDatabase::setApplicationFallbackFontFamilies, Qt >= 6.8).
//  - Typed Latin text (experimental): xochitl's own document font can be replaced (QFont interposition, only for
//    calls made by xochitl's code, so the UI keeps its font).
//  - Korean keyboard: hangul.cpp.
// Settings: /home/root/.config/rmpp-lang/lang.conf (key=value; the Settings page and the rmpp-lang CLI write it).
// State for other add-ons (rmpp-hwr): /home/root/.config/rmpp-lang/state.json.
#include "lang.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QResource>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextDocument>
#include <QtGui/QTextLayout>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlPropertyMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <atomic>
#include <dlfcn.h>
#include <mutex>

namespace rmpplang {

namespace {

const QString kConfDir = QStringLiteral("/home/root/.config/rmpp-lang");
const QString kConf = kConfDir + QStringLiteral("/lang.conf");
const QString kState = kConfDir + QStringLiteral("/state.json");
const QString kProbeReq = kConfDir + QStringLiteral("/probe");
const QString kTraceFlag = kConfDir + QStringLiteral("/trace");
const QString kProbeOut = QStringLiteral("/tmp/rmpp-lang-probe.txt");
const QString kFontRoot = QStringLiteral("/home/root/.local/share/fonts/rmpp-lang");

const Language kLanguages[] = {
    {"ko", "Korean", "한국어", "ko_KR"},
    {"en", "English", "English", ""},
    {"ja", "Japanese", "日本語", ""},
    {"zh", "Chinese", "中文", ""},
    {"latin", "Typed text (Latin)", "Latin", ""},
};

// Families already on the device (or downloaded by xochitl) that make sense per language, best first.
const QHash<QString, QStringList> &knownFamilies() {
    static const QHash<QString, QStringList> h{
        {QStringLiteral("ko"), {QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Noto Sans KR")}},
        {QStringLiteral("ja"), {QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Noto Sans JP")}},
        {QStringLiteral("zh"), {QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Noto Sans SC"),
                                QStringLiteral("Noto Sans CJK TC")}},
        {QStringLiteral("en"), {}},
        {QStringLiteral("latin"), {QStringLiteral("Noto Sans"), QStringLiteral("EB Garamond"),
                                   QStringLiteral("Noto Sans Mono")}},
    };
    return h;
}

// Scripts whose fallback font a language decides. Han is shared by ko/ja/zh: language order decides.
QList<QChar::Script> scriptsOf(const QString &code) {
    if (code == QLatin1String("ko")) return {QChar::Script_Hangul};
    if (code == QLatin1String("ja")) return {QChar::Script_Hiragana, QChar::Script_Katakana};
    if (code == QLatin1String("zh")) return {QChar::Script_Bopomofo};
    return {};
}
bool usesHan(const QString &code) {
    return code == QLatin1String("ko") || code == QLatin1String("ja") || code == QLatin1String("zh");
}

// ---- settings ----

QHash<QString, QString> g_conf;
qint64 g_confStamp = -2;

QString confValue(const QString &key, const QString &def = QString()) { return g_conf.value(key, def); }

QStringList enabledCodes() {
    QStringList out;
    for (const QString &c : confValue(QStringLiteral("languages"), QStringLiteral("ko,en")).split(',')) {
        const QString t = c.trimmed();
        if (!t.isEmpty() && !out.contains(t)) out << t;
    }
    if (!out.contains(QStringLiteral("en"))) out << QStringLiteral("en");
    return out;
}

void loadConf() {
    g_conf.clear();
    QFile f(kConf);
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        if (eq > 0) g_conf.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
}

void saveConf() {
    QDir().mkpath(kConfDir);
    QSaveFile f(kConf);
    if (!f.open(QIODevice::WriteOnly)) return;
    QByteArray out = "# rmpp-lang settings (Settings > Languages and the rmpp-lang command write this file)\n";
    QStringList keys = g_conf.keys();
    keys.sort();
    for (const QString &k : keys) out += (k + '=' + g_conf.value(k) + '\n').toUtf8();
    f.write(out);
    f.commit();
    g_confStamp = QFileInfo(kConf).lastModified().toMSecsSinceEpoch();
}

// ---- fonts ----

struct FontFile { QString path; QString lang; QStringList families; qint64 stamp = 0; int id = -1; };
QHash<QString, FontFile> g_files;          // by path
qint64 g_fontStamp = -2;

qint64 dirStamp() {
    qint64 s = QFileInfo(kFontRoot).lastModified().toMSecsSinceEpoch();
    for (const Language &l : kLanguages) {
        QFileInfo fi(kFontRoot + '/' + QString::fromLatin1(l.code));
        if (fi.exists()) s = qMax(s, fi.lastModified().toMSecsSinceEpoch());
    }
    return s;
}

// Register new or changed font files, drop removed ones.
void scanFonts() {
    QSet<QString> seen;
    for (const Language &l : kLanguages) {
        const QString code = QString::fromLatin1(l.code);
        QDir d(kFontRoot + '/' + code);
        const auto infos = d.entryInfoList({QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc"),
                                            QStringLiteral("*.TTF"), QStringLiteral("*.OTF")}, QDir::Files);
        for (const QFileInfo &fi : infos) {
            const QString p = fi.absoluteFilePath();
            seen.insert(p);
            const qint64 st = fi.lastModified().toMSecsSinceEpoch();
            auto it = g_files.find(p);
            if (it != g_files.end() && it->stamp == st) continue;
            if (it != g_files.end() && it->id >= 0) QFontDatabase::removeApplicationFont(it->id);
            FontFile ff{p, code, {}, st, QFontDatabase::addApplicationFont(p)};
            if (ff.id >= 0) ff.families = QFontDatabase::applicationFontFamilies(ff.id);
            qWarning("[rmpp-lang] font %s (%s): %s", qPrintable(fi.fileName()), qPrintable(code),
                     ff.id >= 0 ? qPrintable(ff.families.join(", ")) : "FAILED");
            g_files.insert(p, ff);
        }
    }
    for (auto it = g_files.begin(); it != g_files.end();) {
        if (seen.contains(it.key())) { ++it; continue; }
        if (it->id >= 0) QFontDatabase::removeApplicationFont(it->id);
        it = g_files.erase(it);
    }
}

// Families offered for a language: installed for it first, then the device's own.
QStringList familiesFor(const QString &code) {
    QStringList out;
    for (const FontFile &f : std::as_const(g_files))
        if (f.lang == code)
            for (const QString &fam : f.families) if (!out.contains(fam)) out << fam;
    const QStringList all = QFontDatabase::families();
    for (const QString &fam : knownFamilies().value(code))
        if (all.contains(fam) && !out.contains(fam)) out << fam;
    return out;
}

// The family set for a language, or "" for the default.
QString fontOf(const QString &code) { return confValue(QStringLiteral("font.") + code); }

// Latin base font of typed text (experimental): families xochitl uses for document text -> replacement.
std::mutex g_mapMutex;
QHash<QString, QString> g_docFontMap;
std::atomic<bool> g_docMapOn{false};
std::atomic<bool> g_trace{false};

void applyFonts() {
    const QStringList on = enabledCodes();
    // Per-script fallbacks.
    QHash<QChar::Script, QStringList> want;
    const QList<QChar::Script> managed{QChar::Script_Hangul, QChar::Script_Hiragana, QChar::Script_Katakana,
                                       QChar::Script_Bopomofo, QChar::Script_Han};
    for (const QString &code : on) {
        const QString fam = fontOf(code);
        if (fam.isEmpty()) continue;
        for (QChar::Script s : scriptsOf(code)) want[s] << fam;
        if (usesHan(code)) want[QChar::Script_Han] << fam;     // enabled-language order = Han priority
    }
    for (QChar::Script s : managed) {
        if (QFontDatabase::applicationFallbackFontFamilies(s) != want.value(s))
            QFontDatabase::setApplicationFallbackFontFamilies(s, want.value(s));
    }
    // Typed Latin text.
    {
        std::lock_guard<std::mutex> lock(g_mapMutex);
        g_docFontMap.clear();
        const QString latin = fontOf(QStringLiteral("latin"));
        if (!latin.isEmpty()) {
            const QStringList targets = confValue(QStringLiteral("latin.replaces"),
                                                  QStringLiteral("reMarkable Serif Small")).split(',');
            for (const QString &t : targets) if (!t.trimmed().isEmpty()) g_docFontMap.insert(t.trimmed(), latin);
        }
        g_docMapOn = !g_docFontMap.isEmpty();
    }
    QString msg;
    for (QChar::Script s : managed) msg += QString::number(int(s)) + '=' + want.value(s).join('|') + ' ';
    qWarning("[rmpp-lang] fallbacks: %s latin: %s", qPrintable(msg), qPrintable(fontOf(QStringLiteral("latin"))));
}

void writeState() {
    QJsonArray langs;
    const QStringList on = enabledCodes();
    for (const Language &l : kLanguages) {
        const QString code = QString::fromLatin1(l.code);
        QJsonArray files;
        for (const FontFile &f : std::as_const(g_files))
            if (f.lang == code) files.append(QJsonObject{{"path", f.path}, {"families", QJsonArray::fromStringList(f.families)}});
        langs.append(QJsonObject{
            {"code", code}, {"name", QString::fromUtf8(l.name)}, {"native", QString::fromUtf8(l.native)},
            {"enabled", code == QLatin1String("latin") || on.contains(code)},
            {"priority", on.indexOf(code)},
            {"keyboard", QString::fromLatin1(l.vkLayout).isEmpty() ? QJsonValue() : QJsonValue(keyboardEnabled(code))},
            {"font", fontOf(code)},
            {"families", QJsonArray::fromStringList(familiesFor(code))},
            {"files", files},
        });
    }
    QDir().mkpath(kConfDir);
    QSaveFile f(kState);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"languages", langs}}).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

// ---- QFont interposition (probe and typed-text font) ----

using SetFamilyFn = void (*)(QFont *, const QString &);
using CtorFn = void (*)(QFont *, const QString &, int, int, bool);
using SetDefFontFn = void (*)(QTextDocument *, const QFont &);

const char *callerModule(void *ret) {
    Dl_info info;
    if (ret && dladdr(ret, &info) && info.dli_fname) {
        const char *s = strrchr(info.dli_fname, '/');
        return s ? s + 1 : info.dli_fname;
    }
    return "?";
}
bool fromXochitl(void *ret) { return qstrcmp(callerModule(ret), "xochitl") == 0; }

std::mutex g_traceMutex;
QSet<QString> g_traced;
QStringList g_traceLines;

void trace(const char *what, const QString &family, void *ret) {
    const QString key = QString::fromLatin1(what) + ' ' + family + " <- " + QString::fromLatin1(callerModule(ret));
    std::lock_guard<std::mutex> lock(g_traceMutex);
    if (g_traced.contains(key) || g_traced.size() > 400) return;
    g_traced.insert(key);
    g_traceLines << key;
}

QString docFamily(const QString &family, void *ret) {
    if (!g_docMapOn.load(std::memory_order_relaxed) || !fromXochitl(ret)) return {};
    std::lock_guard<std::mutex> lock(g_mapMutex);
    return g_docFontMap.value(family);
}

} // namespace

// ---- public ----

const Language *languages(int *count) {
    if (count) *count = int(std::size(kLanguages));
    return kLanguages;
}
bool languageEnabled(const QString &code) { return enabledCodes().contains(code); }
bool keyboardEnabled(const QString &code) {
    return languageEnabled(code) && confValue(QStringLiteral("keyboard.") + code, QStringLiteral("1")) != QLatin1String("0");
}

} // namespace rmpplang

// Interposed Qt symbols (C linkage with the C++ mangled names; the preload wins over libQt6Gui).
extern "C" {
void _ZN5QFont9setFamilyERK7QString(QFont *self, const QString &family) {
    static auto real = reinterpret_cast<rmpplang::SetFamilyFn>(dlsym(RTLD_NEXT, "_ZN5QFont9setFamilyERK7QString"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed)) rmpplang::trace("QFont::setFamily", family, ret);
    const QString to = rmpplang::docFamily(family, ret);
    if (to.isEmpty()) { real(self, family); return; }
    self->setFamilies({to, family});
}
void _ZN5QFontC1ERK7QStringiib(QFont *self, const QString &family, int size, int weight, bool italic) {
    static auto real = reinterpret_cast<rmpplang::CtorFn>(dlsym(RTLD_NEXT, "_ZN5QFontC1ERK7QStringiib"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed)) rmpplang::trace("QFont(family)", family, ret);
    real(self, family, size, weight, italic);
    const QString to = rmpplang::docFamily(family, ret);
    if (!to.isEmpty()) self->setFamilies({to, family});
}
void _ZN13QTextDocument14setDefaultFontERK5QFont(QTextDocument *self, const QFont &font) {
    static auto real = reinterpret_cast<rmpplang::SetDefFontFn>(dlsym(RTLD_NEXT, "_ZN13QTextDocument14setDefaultFontERK5QFont"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed))
        rmpplang::trace("QTextDocument::setDefaultFont", font.families().join('|'), ret);
    real(self, font);
}
// QTextLayout::setFont: how xochitl lays out typed text on the canvas (also used for the typed-text font).
void _ZN11QTextLayout7setFontERK5QFont(QTextLayout *self, const QFont &font) {
    using Fn = void (*)(QTextLayout *, const QFont &);
    static auto real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "_ZN11QTextLayout7setFontERK5QFont"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed)) {
        const bool gui = QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
        rmpplang::trace(gui ? "QTextLayout::setFont [gui thread]" : "QTextLayout::setFont [other thread]",
                        font.families().join('|'), ret);
    }
    const QString to = rmpplang::docFamily(font.family(), ret);
    if (to.isEmpty()) { real(self, font); return; }
    QFont f(font);
    f.setFamilies({to, font.family()});
    real(self, f);
}
}

namespace rmpplang {
namespace {

// ---- probe: what fonts are in use (rmpp-lang probe) ----

void probe() {
    QStringList out;
    out << QStringLiteral("time ") + QDateTime::currentDateTime().toString(Qt::ISODate);
    out << QStringLiteral("enabled ") + enabledCodes().join(',');
    for (QChar::Script s : {QChar::Script_Latin, QChar::Script_Hangul, QChar::Script_Han, QChar::Script_Hiragana})
        out << QStringLiteral("fallback script %1: %2").arg(int(s)).arg(QFontDatabase::applicationFallbackFontFamilies(s).join(" | "));
    out << QStringLiteral("app font default: ") + QGuiApplication::font().family();
    QStringList fams;
    for (const QString &f : QFontDatabase::families())
        if (f.contains(QLatin1String("reMarkable")) || f.contains(QLatin1String("Noto")) || f.contains(QLatin1String("Garamond"))
            || g_files.size() < 50)
            fams << f;
    out << QStringLiteral("families: ") + fams.join(" | ");
    for (const FontFile &f : std::as_const(g_files))
        out << QStringLiteral("file %1 [%2] id=%3 %4").arg(f.path, f.lang).arg(f.id).arg(f.families.join(" | "));
    // Text items now on screen and their fonts.
    QHash<QString, int> itemFonts;
    std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *it, int depth) {
        if (!it || depth > 200) return;
        const char *cls = it->metaObject()->className();
        if (strstr(cls, "Text")) {
            const QVariant v = it->property("font");
            if (v.canConvert<QFont>()) itemFonts[QString::fromLatin1(cls) + ": " + v.value<QFont>().families().join('|')]++;
        }
        for (QQuickItem *k : it->childItems()) walk(k, depth + 1);
    };
    for (QWindow *w : QGuiApplication::allWindows())
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) walk(qw->contentItem(), 0);
    for (auto it = itemFonts.cbegin(); it != itemFonts.cend(); ++it) out << QStringLiteral("item %1 x%2").arg(it.key()).arg(it.value());
    {
        std::lock_guard<std::mutex> lock(g_traceMutex);
        out << QStringLiteral("trace (%1): %2").arg(g_trace ? "on" : "off").arg(g_traceLines.size());
        out << g_traceLines;
    }
    QFile f(kProbeOut);
    if (f.open(QIODevice::WriteOnly)) f.write(out.join('\n').toUtf8() + '\n');
    qWarning("[rmpp-lang] probe written to %s", qPrintable(kProbeOut));
}

// ---- Settings > Languages ----

QQmlPropertyMap *g_map = nullptr;

void publish() {
    if (!g_map) return;
    QJsonArray arr;
    const QStringList on = enabledCodes();
    for (const Language &l : kLanguages) {
        const QString code = QString::fromLatin1(l.code);
        QStringList fams = familiesFor(code);
        arr.append(QJsonObject{
            {"code", code}, {"name", QString::fromUtf8(l.name)}, {"native", QString::fromUtf8(l.native)},
            {"enabled", code == QLatin1String("latin") || on.contains(code)},
            {"fixed", code == QLatin1String("en") || code == QLatin1String("latin")},
            {"hasKeyboard", *l.vkLayout != 0},
            {"keyboard", keyboardEnabled(code)},
            {"font", fontOf(code)},
            {"families", QJsonArray::fromStringList(fams)},
            {"fontDir", kFontRoot + '/' + code},
        });
    }
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (g_map->value(QStringLiteral("languages")).toString() != json) g_map->insert(QStringLiteral("languages"), json);
}

void applyAll(const char *why) {
    scanFonts();
    applyFonts();
    writeState();
    publish();
    qWarning("[rmpp-lang] applied (%s)", why);
}

void command(const QString &cmd) {
    const QString verb = cmd.section(':', 0, 0), code = cmd.section(':', 1, 1), arg = cmd.section(':', 2);
    if (verb == QLatin1String("enable")) {
        QStringList on = enabledCodes();
        if (arg == QLatin1String("1") && !on.contains(code)) on << code;
        if (arg == QLatin1String("0") && code != QLatin1String("en")) on.removeAll(code);
        g_conf.insert(QStringLiteral("languages"), on.join(','));
    } else if (verb == QLatin1String("keyboard")) {
        g_conf.insert(QStringLiteral("keyboard.") + code, arg == QLatin1String("1") ? QStringLiteral("1") : QStringLiteral("0"));
    } else if (verb == QLatin1String("font")) {
        g_conf.insert(QStringLiteral("font.") + code, arg);
    } else if (verb == QLatin1String("up")) {     // raise priority (Han characters use the first CJK language)
        QStringList on = enabledCodes();
        const int i = on.indexOf(code);
        if (i > 0) on.swapItemsAt(i, i - 1);
        g_conf.insert(QStringLiteral("languages"), on.join(','));
    } else {
        return;
    }
    saveConf();
    applyAll("settings");
}

QQuickItem *findItem(QQuickItem *root, const QString &name, int depth = 0) {
    if (!root || depth > 200) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *k : root->childItems())
        if (QQuickItem *f = findItem(k, name, depth + 1)) return f;
    return nullptr;
}

QQuickItem *createFrom(QQmlEngine *eng, QQmlContext *ctx, const char *url, QQuickItem *parent) {
    QQmlComponent comp(eng, QUrl(QString::fromLatin1(url)));
    QObject *o = comp.create(ctx);
    if (!o) {
        for (const auto &e : comp.errors()) qWarning("[rmpp-lang] %s", qPrintable(e.toString()));
        return nullptr;
    }
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) { delete o; return nullptr; }
    item->setParentItem(parent);
    item->setParent(parent);
    return item;
}

void tryInject() {
    for (QWindow *w : QGuiApplication::allWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw) continue;
        QQuickItem *col = findItem(qw->contentItem(), QStringLiteral("settingsColumn"));
        if (!col || col->property("_rmpplang").toBool()) continue;
        col->setProperty("_rmpplang", true);
        QQuickItem *settings = col;
        while (settings && settings->objectName() != QLatin1String("Settings")) settings = settings->parentItem();
        QQuickItem *loader = settings ? findItem(settings, QStringLiteral("payloadLoader")) : nullptr;
        QQmlEngine *eng = settings ? qmlEngine(settings) : nullptr;
        QQmlContext *base = settings ? qmlContext(settings) : nullptr;
        if (!settings || !loader || !eng || !base) {
            qWarning("[rmpp-lang] settings page found but parts missing");
            continue;
        }
        auto *ctx = new QQmlContext(base, settings);
        ctx->setContextProperty(QStringLiteral("lang"), g_map);
        ctx->setContextProperty(QStringLiteral("settingsRoot"), settings);
        ctx->setContextProperty(QStringLiteral("payloadLoaderRef"), loader);
        QQuickItem *row = createFrom(eng, ctx, "qrc:/rmpplang/qml/LangSidebarItem.qml", col);
        if (row) {
            // Right after xochitl's own language row if it can be found, else at the end.
            auto place = [col, row] {
                for (QQuickItem *k : col->childItems()) {
                    if (k == row) continue;
                    const QString t = k->property("text").toString() + k->property("iconSource").toString();
                    if (t.contains(QLatin1String("anguage"))) { row->stackAfter(k); break; }
                }
            };
            place();
            for (int ms : {1500, 4000}) QTimer::singleShot(ms, row, place);
        }
        QQuickItem *page = createFrom(eng, ctx, "qrc:/rmpplang/qml/LangPage.qml", settings);
        qWarning("[rmpp-lang] injected into Settings: row=%s page=%s", row ? "ok" : "FAILED", page ? "ok" : "FAILED");
    }
}

void poll() {
    const QFileInfo ci(kConf);
    const qint64 cs = ci.exists() ? ci.lastModified().toMSecsSinceEpoch() : -1;
    const qint64 fs = dirStamp();
    if (cs != g_confStamp || fs != g_fontStamp) {
        g_confStamp = cs;
        g_fontStamp = fs;
        loadConf();
        applyAll("files changed");
    }
    g_trace = QFileInfo::exists(kTraceFlag);
    if (QFileInfo::exists(kProbeReq)) {
        QFile::remove(kProbeReq);
        probe();
    }
    tryInject();
}

void init() {
    const QString rcc = qEnvironmentVariable("RMPPLANG_RCC", QStringLiteral("/usr/lib/rmpp-lang/rmpp-lang.rcc"));
    const bool ok = QResource::registerResource(rcc);
    qWarning("[rmpp-lang] resources %s: %s", qPrintable(rcc), ok ? "registered" : "FAILED");
    g_trace = QFileInfo::exists(kTraceFlag);   // /home may not be mounted yet: poll() checks again
    auto *app = QCoreApplication::instance();
    g_map = new QQmlPropertyMap(app);
    g_map->insert(QStringLiteral("languages"), QStringLiteral("[]"));
    g_map->insert(QStringLiteral("command"), QString());
    QObject::connect(g_map, &QQmlPropertyMap::valueChanged, app, [](const QString &key, const QVariant &v) {
        if (key == QLatin1String("command")) command(v.toString().section('#', 0, 0));
    });
    loadConf();
    hangulInit();
    auto *t = new QTimer(app);
    QObject::connect(t, &QTimer::timeout, app, [] { poll(); });
    t->start(1500);
    QTimer::singleShot(0, app, [] { poll(); });
}

} // namespace
} // namespace rmpplang

static void rmppLangInit() { rmpplang::init(); }
Q_COREAPP_STARTUP_FUNCTION(rmppLangInit)
