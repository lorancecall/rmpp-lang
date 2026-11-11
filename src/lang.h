// rmpp-lang: shared declarations between the language core (lang.cpp) and the Korean keyboard (hangul.cpp).
#pragma once
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace rmpplang {

// Languages: en and latin are built in; all others are add-ons (/home/root/.local/share/rmpp-lang/languages/<code>/).
bool languageEnabled(const QString &code);
bool keyboardEnabled(const QString &code);     // enabled, has an input method, keyboard.<code> not 0

// Input language of physical keyboards: Ctrl+Space / right Alt cycle through "en" + the enabled languages with
// a keyboard, in priority order. With only Korean on this is the Korean/English toggle.
QStringList inputCycle();
QString currentInput();
void setInput(const QString &code);
void cycleInput();
QString inputLabel();                            // the add-on's label ("한") or the code ("EN", "RU")
QString inputMethod(const QString &code);        // layout | compose:<name> | none
QString screenLayoutFor(const QString &code);    // on-screen layout id of a language ("" = none)
QString inputForScreenLayout(const QString &layout);

// Hangul composition (hangul.cpp), used by add-ons with input=compose:hangul: started once at startup.
void hangulInit();

// Conversion (hanja.cpp): an add-on's convert= kind ("hanja", "" = none) and the absolute path of its convert_dict.
QString convertKind(const QString &code);
QString convertDict(const QString &code);
void hanjaInit();

} // namespace rmpplang
