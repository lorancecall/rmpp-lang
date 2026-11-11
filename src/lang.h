// rmpp-lang: shared declarations between the language core (lang.cpp) and the Korean keyboard (hangul.cpp).
#pragma once
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace rmpplang {

// Languages: ko, en, latin are built in; others are add-ons (/home/root/.local/share/rmpp-lang/languages/<code>/).
bool languageEnabled(const QString &code);
bool keyboardEnabled(const QString &code);     // enabled, has an input method, keyboard.<code> not 0

// Input language of physical keyboards: Ctrl+Space / right Alt cycle through "en" + the enabled languages with
// a keyboard, in priority order. With only Korean on this is the Korean/English toggle.
QStringList inputCycle();
QString currentInput();
void setInput(const QString &code);
void cycleInput();
QString inputLabel();                            // "한", "EN", "RU", ...
QString screenLayoutFor(const QString &code);    // on-screen layout id of a language ("" = none)
QString inputForScreenLayout(const QString &layout);

// Korean keyboard (hangul.cpp): started once at startup.
void hangulInit();

} // namespace rmpplang
