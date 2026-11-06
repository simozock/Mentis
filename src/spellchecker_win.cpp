// FEAT-044 stage A: Windows Spell Checking API (Windows 8 and later), the checker and the user's
// dictionary of the system; no library to ship.
// https://learn.microsoft.com/en-us/windows/win32/intl/spell-checker-api
#include "spellchecker.h"

#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>

namespace {

LPCWSTR wide(const QString &text) { return reinterpret_cast<LPCWSTR>(text.utf16()); }

// Next strings of an IEnumString, freed with CoTaskMemFree as the API requires.
QStringList takeStrings(IEnumString *strings, int max) {
    QStringList result;
    LPOLESTR string = nullptr;
    while (result.size() < max && strings->Next(1, &string, nullptr) == S_OK) {
        result.append(QString::fromWCharArray(string));
        CoTaskMemFree(string);
    }
    return result;
}

// languageTag if supported, otherwise the first supported tag of the same language ("it" for "it-CH").
QString supportedLanguage(ISpellCheckerFactory *factory, const QString &languageTag) {
    BOOL supported = FALSE;
    if (SUCCEEDED(factory->IsSupported(wide(languageTag), &supported)) && supported) return languageTag;
    IEnumString *languages = nullptr;
    if (FAILED(factory->get_SupportedLanguages(&languages)) || !languages) return {};
    const QString language = languageTag.section(QLatin1Char('-'), 0, 0);
    QString match;
    for (const QString &tag : takeStrings(languages, 1000)) {
        if (tag.section(QLatin1Char('-'), 0, 0).compare(language, Qt::CaseInsensitive) == 0) { match = tag; break; }
    }
    languages->Release();
    return match;
}

class WindowsBackend final : public SpellCheck::Backend {
public:
    explicit WindowsBackend(ISpellChecker *checker) : m_checker(checker) {}
    ~WindowsBackend() override { m_checker->Release(); }

    QList<SpellCheck::Range> misspelled(const QString &text) override {
        QList<SpellCheck::Range> ranges;
        IEnumSpellingError *errors = nullptr;
        if (FAILED(m_checker->Check(wide(text), &errors)) || !errors) return ranges;
        ISpellingError *error = nullptr;
        while (errors->Next(&error) == S_OK) {
            ULONG start = 0;
            ULONG length = 0;
            CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
            error->get_StartIndex(&start);
            error->get_Length(&length);
            error->get_CorrectiveAction(&action);
            // A repeated word ("the the", CORRECTIVE_ACTION_DELETE) is grammar, a non-goal.
            if (action != CORRECTIVE_ACTION_DELETE && length > 0)
                ranges.append({static_cast<int>(start), static_cast<int>(length)});
            error->Release();
        }
        errors->Release();
        return ranges;
    }

    QStringList suggestions(const QString &word, int max) override {
        IEnumString *strings = nullptr;
        if (FAILED(m_checker->Suggest(wide(word), &strings)) || !strings) return {};
        QStringList result = takeStrings(strings, max);
        strings->Release();
        return result;
    }

    void learn(const QString &word) override { m_checker->Add(wide(word)); }
    void ignore(const QString &word) override { m_checker->Ignore(wide(word)); }

private:
    ISpellChecker *m_checker;
};

}  // namespace

namespace SpellCheck {

std::unique_ptr<Backend> createSystemBackend(const QString &languageTag) {
    // Qt's Windows platform already initializes COM on the UI thread (S_FALSE here); the offscreen
    // platform of the tests does not. Never uninitialized, so the checker can be released at exit.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return nullptr;
    ISpellCheckerFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(ISpellCheckerFactory), reinterpret_cast<void **>(&factory))))
        return nullptr;
    ISpellChecker *checker = nullptr;
    const QString language = supportedLanguage(factory, languageTag);
    if (!language.isEmpty() && FAILED(factory->CreateSpellChecker(wide(language), &checker))) checker = nullptr;
    factory->Release();
    if (!checker) return nullptr;
    return std::make_unique<WindowsBackend>(checker);
}

}  // namespace SpellCheck
