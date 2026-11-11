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

#include <algorithm>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QResource>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetricsF>
#include <QtGui/QRawFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
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
const QString kFontsJson = kConfDir + QStringLiteral("/fonts.json");   // per language: family + font file (rmpp-hwr)
const QString kProbeReq = kConfDir + QStringLiteral("/probe");
const QString kTraceFlag = kConfDir + QStringLiteral("/trace");
const QString kProbeOut = QStringLiteral("/tmp/rmpp-lang-probe.txt");
const QString kFontRoot = QStringLiteral("/home/root/.local/share/fonts/rmpp-lang");

const QString kAddonRoot = QStringLiteral("/home/root/.local/share/rmpp-lang/languages");   // manifests, keymaps, fonts
const QString kAddonRcc = QStringLiteral("/usr/lib/rmpp-lang/languages");                 // <code>.rcc (layouts)

// A language: built into the core (ko, en, latin) or an add-on described by languages/<code>/language.conf.
struct Lang {
    QString code, name, native, input, defaultFont, dir;   // input: layout | compose:<name> | none
    QString label, keyboardName, sample, samplePlain;     // indicator text, layout name, Settings sample line
                                                          // (+ the line for fonts that cannot render it)
    QString convert, convertDict, convertMap;             // conversion kind (hanja, kana, pinyin), dictionary, reading table
    bool keyboardDefault = true;                          // keyboard on until the user changes it (keyboard_default=)
    QStringList requires;                                 // add-ons this one needs (ja, zh: ko, whose layout they type on)
    QString convertSimplify;                              // Traditional -> Simplified table for candidates (zh hanja)
    QHash<QString, QString> kv;                           // the manifest, for input methods (key@method=)
    QStringList methods;                                  // input methods the user can choose (methods=), first = default
    QString method;                                       // the chosen one ("" = the add-on has no choice)
    QList<QChar::Script> scripts;
    QStringList layouts, known;                           // on-screen layout ids; device families worth offering
    bool han = false, rtl = false, builtin = false;
    QHash<QChar, QPair<QString, QString>> keys, altgr;    // US key character -> (plain, shifted)
};
QList<Lang> g_langs;
qint64 g_addonStamp = -2;

const Lang *findLang(const QString &code) {
    for (const Lang &l : g_langs) if (l.code == code) return &l;
    return nullptr;
}

QChar::Script scriptByName(const QString &n) {
    static const QHash<QString, QChar::Script> t{
        {"Latin", QChar::Script_Latin}, {"Greek", QChar::Script_Greek}, {"Cyrillic", QChar::Script_Cyrillic},
        {"Armenian", QChar::Script_Armenian}, {"Hebrew", QChar::Script_Hebrew}, {"Arabic", QChar::Script_Arabic},
        {"Devanagari", QChar::Script_Devanagari}, {"Bengali", QChar::Script_Bengali}, {"Thai", QChar::Script_Thai},
        {"Lao", QChar::Script_Lao}, {"Georgian", QChar::Script_Georgian}, {"Hangul", QChar::Script_Hangul},
        {"Hiragana", QChar::Script_Hiragana}, {"Katakana", QChar::Script_Katakana},
        {"Bopomofo", QChar::Script_Bopomofo}, {"Han", QChar::Script_Han}, {"Ethiopic", QChar::Script_Ethiopic},
        {"Khmer", QChar::Script_Khmer}, {"Mongolian", QChar::Script_Mongolian}, {"Tibetan", QChar::Script_Tibetan},
    };
    return t.value(n, QChar::Script_Unknown);
}

QHash<QString, QString> readKeyValues(const QString &path) {
    QHash<QString, QString> kv;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return kv;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=', 1);          // a key may itself be "=" (keymap: "= = ...")
        if (eq <= 0) continue;
        QString v = line.mid(eq + 1).trimmed();
        if (const int hash = v.indexOf(QLatin1String(" #")); hash >= 0) v = v.left(hash).trimmed();
        kv.insert(line.left(eq).trimmed(), v);
    }
    return kv;
}

void loadKeymap(Lang &l) {
    const auto kv = readKeyValues(l.dir + QStringLiteral("/keymap.conf"));
    for (auto it = kv.cbegin(); it != kv.cend(); ++it) {
        QString key = it.key();
        auto *table = &l.keys;
        if (key.startsWith(QLatin1String("altgr."))) { table = &l.altgr; key = key.mid(6); }
        if (key.size() != 1) continue;
        const QStringList v = it.value().split(' ', Qt::SkipEmptyParts);
        if (v.isEmpty()) continue;
        table->insert(key.at(0), {v.at(0), v.value(1, v.at(0).toUpper())});
    }
}

// Input method of a language with a choice (zh: hangul | pinyin | hanja): the manifest's key@method values
// override the plain keys. The choice is method.<code> in lang.conf.
QString confValue(const QString &key, const QString &def = QString());
void applyMethod(Lang &l) {
    if (l.methods.isEmpty()) return;
    const QString want = confValue(QStringLiteral("method.") + l.code);
    l.method = l.methods.contains(want) ? want : l.methods.first();
    const auto get = [&](const char *key, const char *def = "") {
        const QString k = QString::fromLatin1(key);
        const auto it = l.kv.constFind(k + QLatin1Char('@') + l.method);
        return it != l.kv.constEnd() ? *it : l.kv.value(k, QString::fromLatin1(def));
    };
    l.input = get("input", "none");
    l.layouts = get("layouts").split(' ', Qt::SkipEmptyParts);
    l.keyboardName = get("keyboard_name");
    l.label = get("label");
    l.convert = get("convert");
    l.convertDict = get("convert_dict");
    l.convertMap = get("convert_map");
    l.convertSimplify = get("convert_simplify");
    l.requires = get("requires").split(',', Qt::SkipEmptyParts);
}
void applyMethods() {
    for (Lang &l : g_langs) applyMethod(l);
}

void loadLanguages() {
    g_langs.clear();
    // The core knows no language but English and the typed-text font; everything else is an add-on.
    Lang en;
    en.code = "en"; en.name = "English"; en.native = "English"; en.input = "layout"; en.builtin = true;
    Lang latin;
    latin.code = "latin"; latin.name = "Typed text (Latin)"; latin.native = "Latin"; latin.input = "none";
    latin.builtin = true; latin.known = {"Noto Sans", "EB Garamond", "Noto Sans Mono"};
    latin.sample = "Hello, have a nice day · Aa Gg Rr 123";
    g_langs << en << latin;

    QDir root(kAddonRoot);
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &d : dirs) {
        const QString dir = root.absoluteFilePath(d);
        const auto kv = readKeyValues(dir + QStringLiteral("/language.conf"));
        const QString code = kv.value("code");
        if (code.isEmpty() || code != d || findLang(code)) {     // also refuses the reserved en / latin
            qWarning("[rmpp-lang] add-on %s: no or bad language.conf, skipped", qPrintable(d));
            continue;
        }
        if (kv.value("format", "1").toInt() > 1) {
            qWarning("[rmpp-lang] add-on %s: manifest format %s is newer than this core", qPrintable(d),
                     qPrintable(kv.value("format")));
            continue;
        }
        Lang l;
        l.code = code; l.dir = dir;
        l.name = kv.value("name", code); l.native = kv.value("native", l.name);
        l.input = kv.value("input", "none");
        l.defaultFont = kv.value("font");
        l.han = kv.value("han") == QLatin1String("1");
        l.rtl = kv.value("rtl") == QLatin1String("1");
        l.layouts = kv.value("layouts").split(' ', Qt::SkipEmptyParts);
        for (const QString &n : kv.value("scripts").split(' ', Qt::SkipEmptyParts))
            if (QChar::Script s = scriptByName(n); s != QChar::Script_Unknown) l.scripts << s;
        l.label = kv.value("label");
        l.keyboardName = kv.value("keyboard_name");
        l.sample = kv.value("sample");
        l.samplePlain = kv.value("sample_plain");
        l.convert = kv.value("convert");
        l.convertDict = kv.value("convert_dict");
        l.convertMap = kv.value("convert_map");
        l.keyboardDefault = kv.value("keyboard_default", "1") != QLatin1String("0");
        l.requires = kv.value("requires").split(',', Qt::SkipEmptyParts);
        l.kv = kv;
        l.methods = kv.value("methods").split(' ', Qt::SkipEmptyParts);
        applyMethod(l);
        if (!l.defaultFont.isEmpty()) l.known << l.defaultFont;
        for (const QString &f : kv.value("families").split(',', Qt::SkipEmptyParts))
            if (!l.known.contains(f.trimmed())) l.known << f.trimmed();
        loadKeymap(l);
        g_langs << l;
        qWarning("[rmpp-lang] add-on %s (%s): input=%s layouts=%s keys=%d", qPrintable(code), qPrintable(l.name),
                 qPrintable(l.input), qPrintable(l.layouts.join(',')), int(l.keys.size() + l.altgr.size()));
    }
}

// Stamp of the add-on folder and its manifests (install, upgrade, removal).
qint64 addonStamp() {
    QFileInfo r(kAddonRoot);
    qint64 s = r.exists() ? r.lastModified().toMSecsSinceEpoch() : -1;
    for (const QFileInfo &fi : QDir(kAddonRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        s = qMax(s, fi.lastModified().toMSecsSinceEpoch());
        QFileInfo m(fi.absoluteFilePath() + QStringLiteral("/language.conf"));
        if (m.exists()) s = qMax(s, m.lastModified().toMSecsSinceEpoch());
    }
    return s;
}

// On-screen layouts of add-ons: registered at startup (they must exist when xochitl reads its keyboard list).
void registerAddonLayouts() {
    for (const QFileInfo &fi : QDir(kAddonRcc).entryInfoList({QStringLiteral("*.rcc")}, QDir::Files)) {
        const bool ok = QResource::registerResource(fi.absoluteFilePath());
        qWarning("[rmpp-lang] layouts %s: %s", qPrintable(fi.fileName()), ok ? "registered" : "FAILED");
    }
}

// ---- settings ----

QHash<QString, QString> g_conf;
qint64 g_confStamp = -2;

QString confValue(const QString &key, const QString &def) { return g_conf.value(key, def); }

QStringList enabledCodes() {
    QStringList out;
    for (const QString &c : confValue(QStringLiteral("languages"), QStringLiteral("en")).split(',')) {
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

// Font folders of a language: the user's, then the add-on's own.
QStringList fontDirs(const Lang &l) {
    QStringList d{kFontRoot + '/' + l.code};
    if (!l.dir.isEmpty()) d << l.dir + QStringLiteral("/fonts");
    return d;
}

qint64 dirStamp() {
    qint64 s = QFileInfo(kFontRoot).lastModified().toMSecsSinceEpoch();
    for (const Lang &l : g_langs)
        for (const QString &dir : fontDirs(l)) {
            QFileInfo fi(dir);
            if (fi.exists()) s = qMax(s, fi.lastModified().toMSecsSinceEpoch());
        }
    return s;
}

// Register new or changed font files, drop removed ones.
void scanFonts() {
    QSet<QString> seen;
    for (const Lang &l : g_langs) for (const QString &dirPath : fontDirs(l)) {
        const QString code = l.code;
        QDir d(dirPath);
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
    if (const Lang *l = findLang(code))
        for (const QString &fam : l->known)
            if (all.contains(fam) && !out.contains(fam)) out << fam;
    return out;
}

// The family chosen for a language (lang.conf, else the add-on's default), or "" for the device default.
QString fontOf(const QString &code) {
    const QString key = QStringLiteral("font.") + code;
    if (g_conf.contains(key)) return g_conf.value(key);
    const Lang *l = findLang(code);
    return l ? l->defaultFont : QString();
}

// Latin base font of typed text (experimental): families xochitl uses for document text -> replacement.
std::mutex g_mapMutex;
QHash<QString, QString> g_docFontMap;
std::atomic<bool> g_docMapOn{false};
std::atomic<bool> g_trace{false};

void applyFonts() {
    const QStringList on = enabledCodes();
    // Per-script fallbacks.
    QHash<QChar::Script, QStringList> want;
    static QSet<QChar::Script> managedSet{QChar::Script_Han};    // every script ever set, so it can be cleared
    for (const QString &code : on) {
        const Lang *l = findLang(code);
        const QString fam = fontOf(code);
        if (!l || fam.isEmpty()) continue;
        for (QChar::Script s : l->scripts) { want[s] << fam; managedSet.insert(s); }
        if (l->han) want[QChar::Script_Han] << fam;     // enabled-language order = Han priority
    }
    QList<QChar::Script> managed = managedSet.values();
    std::sort(managed.begin(), managed.end());
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

QString deviceDefault(const Lang &l);

// A font file for a family, for renderers outside Qt (rmpp-hwr's ink glyphs): a file rmpp-lang registered (the
// "Regular" one if there are several), else fontconfig's match. Returns {file, family of that file}.
QPair<QString, QString> fontFileFor(const QString &family) {
    QString best;
    for (const FontFile &f : std::as_const(g_files)) {
        if (!f.families.contains(family)) continue;
        const QString name = QFileInfo(f.path).fileName();
        if (best.isEmpty() || name.contains(QLatin1String("Regular"), Qt::CaseInsensitive)) best = f.path;
    }
    if (!best.isEmpty()) return {best, family};
    QProcess p;
    p.start(QStringLiteral("fc-match"), {QStringLiteral("-f"), QStringLiteral("%{file}\n%{family[0]}"),
                                          family + QStringLiteral(":style=Regular")});
    if (!p.waitForFinished(3000)) return {};
    const QStringList out = QString::fromUtf8(p.readAllStandardOutput()).split('\n');
    return {out.value(0).trimmed(), out.value(1).trimmed()};
}

// fonts.json: the one font per language that other add-ons draw with (rmpp-hwr Convert to ink).
//   {"version": 1, "languages": {"ko": {"family": "...", "file": "/....otf", "chosen": "..."}, ...}}
// "chosen" is the user's choice ("" = default); "family"/"file" what is actually used. xochitl's own
// "reMarkable Sans" has no file: then fontconfig's substitute is given (its family in "family").
void writeFontsJson() {
    QJsonObject langs;
    for (const Lang &l : g_langs) {
        const QString used = fontOf(l.code);                                   // user's choice, else the add-on's default
        const QString chosen = confValue(QStringLiteral("font.") + l.code);    // the user's choice only
        const QString family = used.isEmpty() ? deviceDefault(l) : used;
        const auto ff = fontFileFor(family);
        langs.insert(l.code, QJsonObject{{"family", ff.second.isEmpty() ? family : ff.second},
                                         {"file", ff.first}, {"chosen", chosen}});
    }
    QSaveFile f(kFontsJson);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"version", 1}, {"languages", langs}}).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void writeState() {
    QJsonArray langs;
    const QStringList on = enabledCodes();
    for (const Lang &l : g_langs) {
        const QString code = l.code;
        QJsonArray files;
        for (const FontFile &f : std::as_const(g_files))
            if (f.lang == code) files.append(QJsonObject{{"path", f.path}, {"families", QJsonArray::fromStringList(f.families)}});
        langs.append(QJsonObject{
            {"code", code}, {"name", l.name}, {"native", l.native},
            {"addon", !l.builtin},
            {"enabled", code == QLatin1String("latin") || on.contains(code)},
            {"priority", on.indexOf(code)},
            {"input", l.input},
            {"layouts", QJsonArray::fromStringList(l.layouts)},
            {"rtl", l.rtl},
            {"keyboard", l.input == QLatin1String("none") || code == QLatin1String("en") ? QJsonValue()
                                                                                         : QJsonValue(keyboardEnabled(code))},
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
    writeFontsJson();
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

QString convertKind(const QString &code) {
    const Lang *l = findLang(code);
    return l && !l->convertDict.isEmpty() ? l->convert : QString();
}

// A file of an add-on: "dict/x.txt" in its own folder, "@ko/dict/hanja.txt" in another add-on's.
QString addonFile(const Lang *l, const QString &rel) {
    if (!l || rel.isEmpty() || rel.contains(QLatin1String(".."))) return {};
    if (rel.startsWith(QLatin1Char('@'))) {
        const Lang *other = findLang(rel.mid(1).section(QLatin1Char('/'), 0, 0));
        return other && !other->dir.isEmpty() ? other->dir + QLatin1Char('/') + rel.section(QLatin1Char('/'), 1) : QString();
    }
    return l->dir + QLatin1Char('/') + rel;
}

QString convertDict(const QString &code) { return addonFile(findLang(code), findLang(code) ? findLang(code)->convertDict : QString()); }

QString convertSimplify(const QString &code) {
    const Lang *l = findLang(code);
    return l ? addonFile(l, l->convertSimplify) : QString();
}

QString convertMap(const QString &code) {
    const Lang *l = findLang(code);
    return l ? addonFile(l, l->convertMap) : QString();
}

bool languageEnabled(const QString &code) { return findLang(code) && enabledCodes().contains(code); }
bool keyboardEnabled(const QString &code) {
    const Lang *l = findLang(code);
    return l && l->input != QLatin1String("none") && languageEnabled(code)
        && std::all_of(l->requires.begin(), l->requires.end(), [](const QString &r) { return findLang(r.trimmed()) != nullptr; })
        && confValue(QStringLiteral("keyboard.") + code, l->keyboardDefault ? QStringLiteral("1") : QStringLiteral("0"))
               != QLatin1String("0");
}

// ---- input language for physical keyboards (Ctrl+Space / right Alt cycle) ----

namespace { QString g_input = QStringLiteral("en"); }

QStringList inputCycle() {
    QStringList out{QStringLiteral("en")};
    for (const QString &c : enabledCodes())
        if (c != QLatin1String("en") && keyboardEnabled(c)) out << c;
    return out;
}
QString currentInput() {
    if (!inputCycle().contains(g_input)) g_input = QStringLiteral("en");
    return g_input;
}
void setInput(const QString &code) {
    const QString want = inputCycle().contains(code) ? code : QStringLiteral("en");
    if (want != g_input) qWarning("[rmpp-lang] input language: %s", qPrintable(want));
    g_input = want;
}
void cycleInput() {
    const QStringList c = inputCycle();
    setInput(c.at((c.indexOf(currentInput()) + 1) % c.size()));
}
QString screenLayoutFor(const QString &code) {
    const Lang *l = findLang(code);
    return l && !l->layouts.isEmpty() ? l->layouts.first() : QString();
}
QString inputForScreenLayout(const QString &layout) {
    // Several languages can type on one layout (ja / zh are typed in Hangul on ko_KR): keep the current one if it
    // uses this layout, else the first in priority order.
    if (const Lang *cur = findLang(currentInput()); cur && cur->layouts.contains(layout)) return cur->code;
    for (const QString &c : enabledCodes())
        if (const Lang *l = findLang(c); l && l->layouts.contains(layout) && keyboardEnabled(c)) return c;
    return QStringLiteral("en");
}
QString inputChoice(const QString &code) {
    const Lang *l = findLang(code);
    return l ? l->method : QString();
}
QString inputLabel() {
    const QString c = currentInput();
    const Lang *l = findLang(c);
    return l && !l->label.isEmpty() ? l->label : c.left(2).toUpper();
}
QString inputMethod(const QString &code) {
    const Lang *l = findLang(code);
    return l ? l->input : QString();
}

// Physical keys of an add-on with a keymap: the character for a key event, or "" to leave it alone.
QString mapKey(int qtKey, Qt::KeyboardModifiers mods, bool altgr) {
    const Lang *l = findLang(currentInput());
    if (!l || (l->keys.isEmpty() && l->altgr.isEmpty())) return {};
    // Key on a US keyboard, unshifted.
    static const QHash<int, QChar> shiftedToBase{
        {Qt::Key_BraceLeft, '['}, {Qt::Key_BraceRight, ']'}, {Qt::Key_Colon, ';'}, {Qt::Key_QuoteDbl, '\''},
        {Qt::Key_Less, ','}, {Qt::Key_Greater, '.'}, {Qt::Key_Question, '/'}, {Qt::Key_Underscore, '-'},
        {Qt::Key_Plus, '='}, {Qt::Key_Bar, '\\'}, {Qt::Key_AsciiTilde, '`'}, {Qt::Key_Exclam, '1'},
        {Qt::Key_At, '2'}, {Qt::Key_NumberSign, '3'}, {Qt::Key_Dollar, '4'}, {Qt::Key_Percent, '5'},
        {Qt::Key_AsciiCircum, '6'}, {Qt::Key_Ampersand, '7'}, {Qt::Key_Asterisk, '8'}, {Qt::Key_ParenLeft, '9'},
        {Qt::Key_ParenRight, '0'}};
    QChar base;
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) base = QChar(char16_t(u'a' + (qtKey - Qt::Key_A)));
    else if (shiftedToBase.contains(qtKey)) base = shiftedToBase.value(qtKey);
    else if (qtKey > 0x20 && qtKey < 0x7f) base = QChar(char16_t(qtKey));
    else return {};
    const auto &table = altgr ? l->altgr : l->keys;
    auto it = table.find(base);
    if (it == table.end()) return {};
    return (mods & Qt::ShiftModifier) ? it->second : it->first;
}

} // namespace rmpplang

// Interposed Qt symbols (C linkage with the C++ mangled names; the preload wins over libQt6Gui).
// QFont::setFamily / QFont(family) / QTextDocument::setDefaultFont are only traced: replacing "reMarkable Sans" there
// also changed xochitl's whole UI (it sets the application font from its own code). The typed-text font is replaced
// in QTextLayout::setFont called from xochitl's code, which is how it lays out text on the canvas.
extern "C" {
void _ZN5QFont9setFamilyERK7QString(QFont *self, const QString &family) {
    static auto real = reinterpret_cast<rmpplang::SetFamilyFn>(dlsym(RTLD_NEXT, "_ZN5QFont9setFamilyERK7QString"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed)) rmpplang::trace("QFont::setFamily", family, ret);
    real(self, family);
}
void _ZN5QFontC1ERK7QStringiib(QFont *self, const QString &family, int size, int weight, bool italic) {
    static auto real = reinterpret_cast<rmpplang::CtorFn>(dlsym(RTLD_NEXT, "_ZN5QFontC1ERK7QStringiib"));
    void *ret = __builtin_return_address(0);
    if (rmpplang::g_trace.load(std::memory_order_relaxed)) rmpplang::trace("QFont(family)", family, ret);
    real(self, family, size, weight, italic);
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
    // Built-in on-screen layouts (format research: are extra entries of a key's list long-press choices?)
    for (const char *loc : {"de_DE", "fr_FR", "pl_PL", "en_US"}) {
        QFile lf(QStringLiteral(":/misc/keyboards/%1/keyboard_layout.json").arg(QLatin1String(loc)));
        if (lf.open(QIODevice::ReadOnly))
            QFile::copy(lf.fileName(), QStringLiteral("/tmp/rmpp-lang-layout-%1.json").arg(QLatin1String(loc)));
    }
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

// What the tablet shows for a language without rmpp-lang: the first device family it knows of
// (fontconfig's own choice for the script), or xochitl's font.
QString deviceDefault(const Lang &l) {
    if (l.code == QLatin1String("latin") || l.code == QLatin1String("en")) return QStringLiteral("reMarkable Sans");
    const QStringList all = QFontDatabase::families();
    for (const QString &f : l.known)
        if (f != l.defaultFont && all.contains(f)) return f;
    return QStringLiteral("reMarkable Sans");
}

// ---- physical keyboards for add-ons with a keymap.conf ----
// Korean (hangul.cpp) handles the toggle keys and Korean itself; this filter types the add-on's characters while
// its language is the input language.
class KeymapFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj != QGuiApplication::focusObject()) return false;
        if (ev->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(ev);
            const auto mods = k->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
            const bool altgr = (mods == (Qt::ControlModifier | Qt::AltModifier)) || (mods == Qt::GroupSwitchModifier);
            if (mods != Qt::NoModifier && !altgr) return false;
            const QString out = mapKey(k->key(), k->modifiers(), altgr);
            if (out.isEmpty()) return false;
            QInputMethodEvent im;
            im.setCommitString(out);
            QCoreApplication::sendEvent(obj, &im);
            return true;
        }
        if (ev->type() == QEvent::InputMethod) {
            // Type Folio keys can arrive as one-letter input-method commits (see hangul.cpp).
            auto *e = static_cast<QInputMethodEvent *>(ev);
            const QString c = e->commitString();
            if (c.size() != 1 || !e->preeditString().isEmpty() || e->replacementLength() != 0) return false;
            const char16_t u = c.at(0).unicode();
            if (u > 0x7f) return false;
            const bool upper = (u >= u'A' && u <= u'Z');
            const int key = upper ? Qt::Key_A + (u - u'A') : (u >= u'a' && u <= u'z') ? Qt::Key_A + (u - u'a') : int(u);
            const QString out = mapKey(key, upper ? Qt::ShiftModifier : Qt::NoModifier, false);
            if (!out.isEmpty()) e->setCommitString(out);
        }
        return false;
    }
};

// ---- Settings > Languages ----

// Can `family` show `text` as written? Every character needs a glyph in that font (no fallback), and runs of
// conjoining Hangul jamo (old Hangul) must compose: in a font that composes them, the vowels and final consonants
// add no width to the leading consonants (they are placed inside the syllable); a font without the jamo features
// (the OS's Noto Sans KR) sets each jamo side by side.
bool rendersSample(const QString &family, const QString &text) {
    static QHash<QString, bool> cache;
    const QString key = family + QChar(0) + text;
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QFont f(family);
    f.setPixelSize(40);
    const QRawFont raw = QRawFont::fromFont(f);
    bool ok = raw.isValid() && raw.familyName().compare(family, Qt::CaseInsensitive) == 0;
    const auto jamo = [](char32_t c) {
        return (c >= 0x1100 && c <= 0x11FF) || (c >= 0xA960 && c <= 0xA97F) || (c >= 0xD7B0 && c <= 0xD7FF);
    };
    const auto lead = [](char32_t c) { return (c >= 0x1100 && c <= 0x115F) || (c >= 0xA960 && c <= 0xA97F); };
    const QList<uint> cps = text.toUcs4();
    for (uint c : cps)
        if (ok && !QChar::isSpace(c) && !raw.supportsCharacter(c)) ok = false;
    if (ok) {
        const QFontMetricsF fm(f);
        for (qsizetype i = 0; ok && i < cps.size();) {
            if (!jamo(cps[i])) { ++i; continue; }
            QString run, leads;
            for (; i < cps.size() && jamo(cps[i]); ++i) {
                const QString c = QString::fromUcs4(reinterpret_cast<const char32_t *>(&cps[i]), 1);
                run += c;
                if (lead(cps[i])) leads += c;
            }
            const qreal wRun = fm.horizontalAdvance(run), wLeads = fm.horizontalAdvance(leads);
            qWarning("[rmpp-lang] sample check %s: jamo run %.1f px, leading consonants %.1f px", qPrintable(family),
                     wRun, wLeads);
            if (leads.isEmpty() || wRun > wLeads * 1.15) ok = false;
        }
    }
    cache.insert(key, ok);
    return ok;
}

QQmlPropertyMap *g_map = nullptr;

void publish() {
    if (!g_map) return;
    QJsonArray arr;
    const QStringList on = enabledCodes();
    for (const Lang &l : g_langs) {
        const QString code = l.code;
        // The picker lists families only; the default one ("" in lang.conf) is marked, first in the list.
        const QString defaultFamily = l.defaultFont.isEmpty() ? deviceDefault(l) : l.defaultFont;
        QStringList fams = familiesFor(code);
        fams.removeAll(defaultFamily);
        fams.prepend(defaultFamily);
        // Sample line per family: the add-on's `sample`, or `sample_plain` where the font cannot render it.
        QJsonObject samples;
        if (!l.samplePlain.isEmpty())
            for (const QString &fam : fams)
                if (!rendersSample(fam, l.sample)) samples.insert(fam, l.samplePlain);
        arr.append(QJsonObject{
            {"code", code}, {"name", l.name}, {"native", l.native},
            {"addon", !l.builtin},
            {"enabled", code == QLatin1String("latin") || on.contains(code)},
            {"fixed", code == QLatin1String("en") || code == QLatin1String("latin")},
            {"priority", on.indexOf(code)},
            {"hasKeyboard", l.input != QLatin1String("none") && code != QLatin1String("en")},
            {"keyboard", keyboardEnabled(code)},
            {"input", l.input},
            {"layouts", QJsonArray::fromStringList(l.layouts)},
            {"keyboardName", l.keyboardName},
            {"methods", QJsonArray::fromStringList(l.methods)},
            {"methodToggle", l.kv.value(QStringLiteral("method_toggle"))},   // ko: old = an on/off option
            {"methodDescription", l.kv.value(QStringLiteral("method_description"))},
            {"method", l.method},
            {"methodNames", [&] {
                 QJsonObject o;
                 for (const QString &m : l.methods) o.insert(m, l.kv.value(QStringLiteral("method_name.") + m, m));
                 return o;
             }()},
            {"sample", l.sample},
            {"samples", samples},
            {"rtl", l.rtl},
            {"font", confValue(QStringLiteral("font.") + code)},     // the user's choice, "" = default
            {"defaultFamily", defaultFamily},
            {"families", QJsonArray::fromStringList(fams)},
            {"deviceDefault", deviceDefault(l)},
            {"fontDir", kFontRoot + '/' + code},
        });
    }
    const QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (g_map->value(QStringLiteral("languages")).toString() != json) g_map->insert(QStringLiteral("languages"), json);
}

void applyAll(const char *why) {
    applyMethods();
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
    } else if (verb == QLatin1String("method")) {
        g_conf.insert(QStringLiteral("method.") + code, arg);
    } else if (verb == QLatin1String("up") || verb == QLatin1String("down")) {
        // Move one place among the languages in use; English has no font of its own and is skipped.
        QStringList on = enabledCodes();
        const int i = on.indexOf(code);
        const int step = verb == QLatin1String("up") ? -1 : 1;
        int j = i + step;
        while (j >= 0 && j < on.size() && on.at(j) == QLatin1String("en")) j += step;
        if (i >= 0 && j >= 0 && j < on.size()) on.move(i, j);
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

// ---- Settings rail order shared by the rmpp add-ons ----
// Every rmpp row carries an int property "rmppOrder" and goes after xochitl's Wi-Fi row, sorted by it:
//   rmpp-bt 100 (Bluetooth), rmpp-tailscale 200, rmpp-lang 300 (Languages), rmpp-hwr 400 (Handwriting).
// Rows of add-ons that do not set the property yet are recognised by class / objectName. Each add-on places only
// its own row: after the rmpp row with the highest order below its own, else after Wi-Fi. So the final order is
// the same whichever add-on injects first.
const int kRailOrder = 300;
QList<QPair<QPointer<QQuickItem>, QPointer<QQuickItem>>> g_rails;   // (settingsColumn, our row)

int railOrder(QQuickItem *k) {
    const QVariant v = k->property("rmppOrder");
    if (v.isValid()) return v.toInt();
    const char *cls = k->metaObject()->className();
    if (qstrncmp(cls, "BtSidebarItem", 13) == 0) return 100;
    if (qstrncmp(cls, "TsSidebarItem", 13) == 0) return 200;
    if (k->objectName() == QLatin1String("rmppHwrSidebarItem")) return 400;
    return -1;
}

void placeRow(QQuickItem *col, QQuickItem *row) {
    const int mine = railOrder(row);
    QQuickItem *after = nullptr, *wifi = nullptr;
    int best = -1;
    for (QQuickItem *k : col->childItems()) {
        if (k == row) continue;
        if (k->property("iconSource").toString().contains(QLatin1String("/wifi"))) wifi = k;
        const int o = railOrder(k);
        if (o >= 0 && o < mine && o > best) { best = o; after = k; }
    }
    if (!after) after = wifi;
    if (!after) return;
    // Already right after it? (stackAfter on every childrenChanged would loop.)
    const auto kids = col->childItems();
    const int ia = kids.indexOf(after), ir = kids.indexOf(row);
    if (ir != ia + 1) {
        row->stackAfter(after);
        qWarning("[rmpp-lang] rail: Languages placed after %s", qPrintable(after->property("text").toString()));
    }
}

void tryInject();

// Inject as soon as the Settings page exists, before its first frame is drawn (afterAnimating runs on the GUI
// thread before every frame), so the row is there from the start instead of popping in on the next poll.
void watchWindows() {
    for (QWindow *w : QGuiApplication::allWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw || qw->property("_rmpplangWatch").toBool()) continue;
        qw->setProperty("_rmpplangWatch", true);
        QObject::connect(qw, &QQuickWindow::afterAnimating, qw, [] {
            tryInject();
            for (int i = g_rails.size() - 1; i >= 0; --i) {
                if (!g_rails[i].first || !g_rails[i].second) g_rails.removeAt(i);
                else placeRow(g_rails[i].first, g_rails[i].second);
            }
        });
    }
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
            row->setProperty("rmppOrder", kRailOrder);
            placeRow(col, row);
            // Rows of other add-ons arrive in later frames: checked again before every frame (afterAnimating),
            // so the order is right before anything is drawn and nothing moves afterwards.
            g_rails.append({QPointer<QQuickItem>(col), QPointer<QQuickItem>(row)});
        }
        QQuickItem *page = createFrom(eng, ctx, "qrc:/rmpplang/qml/LangPage.qml", settings);
        qWarning("[rmpp-lang] injected into Settings: row=%s page=%s", row ? "ok" : "FAILED", page ? "ok" : "FAILED");
    }
}

void poll() {
    const QFileInfo ci(kConf);
    const qint64 cs = ci.exists() ? ci.lastModified().toMSecsSinceEpoch() : -1;
    if (const qint64 as = addonStamp(); as != g_addonStamp) {
        g_addonStamp = as;
        loadLanguages();
        g_fontStamp = -2;       // fonts of new add-ons
    }
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
    watchWindows();
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
    registerAddonLayouts();
    loadConf();
    loadLanguages();
    auto *keys = new KeymapFilter(app);
    app->installEventFilter(keys);
    hangulInit();             // installed after: sees events first, handles Korean and the language toggle
    hanjaInit();              // after Hangul: the Hanja key and candidate keys come first
    auto *t = new QTimer(app);
    QObject::connect(t, &QTimer::timeout, app, [] { poll(); });
    t->start(1500);
    QTimer::singleShot(0, app, [] { poll(); });
}

} // namespace
} // namespace rmpplang

static void rmppLangInit() { rmpplang::init(); }
Q_COREAPP_STARTUP_FUNCTION(rmppLangInit)
