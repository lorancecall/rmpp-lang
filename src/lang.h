// rmpp-lang: shared declarations between the language core (lang.cpp) and the keyboards (hangul.cpp).
#pragma once
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace rmpplang {

// Language codes shared with rmpp-hwr: ko, en, ja, zh, latin ("latin" = typed Latin text in general).
struct Language {
    const char *code;
    const char *name;         // English name shown in Settings
    const char *native;       // name in the language itself
    const char *vkLayout;     // on-screen keyboard layout added by rmpp-lang ("" = none yet)
};
const Language *languages(int *count);

// Current settings (from /home/root/.config/rmpp-lang/lang.conf).
bool languageEnabled(const QString &code);
bool keyboardEnabled(const QString &code);

// Korean keyboard (hangul.cpp): started once at startup, keyboardEnabled("ko") is checked live.
void hangulInit();

} // namespace rmpplang
