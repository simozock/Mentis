<p align="center">
  <img src="../assets/appicon/mentis-128.png" alt="Mentis" width="72">
</p>

<h1 align="center">Translations</h1>

<p align="center"><a href="../README.md">← Back to the README</a></p>

Mentis is written in English; every other language is one JSON file in this folder.

| File | What it is |
|---|---|
| `<code>.json` | One language: `it.json`, `de.json`, `pt_BR.json`, `zh_Hans.json`... |
| `template.json` | Every text of the interface, none translated: a new language starts from a copy of it |

## For translators

You only need a text editor, or GitHub's own editor in the browser. Each file is a list of the
windows of Mentis, and in each window the English text on the left and its translation on the right:

```json
{
  "GanttView": {
    "Later": "In ritardo",
    "%n task(s) removed": ["%n attività tolta", "%n attività tolte"],
    "Bold ‖ branch width": "Grassetto"
  }
}
```

- **Translate only the right side.** Never change the English text on the left: it is how Mentis
  finds the translation.
- **An empty translation (`""`) is not translated yet**: Mentis shows the English text.
- **The same English text can appear in several windows**: translate each one for its place (a width
  can be "Larghezza" in one window and "Spessore" in another).
- **`‖` adds a hint** about where the text is used (`Bold ‖ branch width`): translate only the text,
  the hint is not shown.
- **Plurals are a list**, one form per plural form of your language (Italian and Spanish 2, Polish 3).
  `%n` is the number.
- **Keep the placeholders** `%1`, `%2`, `%n` and the `&` before a letter (a keyboard shortcut).
- Keep the quotes, the commas and the colons: the file must stay valid JSON. The build says the line
  of a mistake.

A language appears in **Settings** › **General** › **Language** when at least **80%** of its texts
are translated. The language changes the next time Mentis starts.

## Adding a language

1. Copy `template.json` to `<code>.json`: the language code, with a territory or a script when needed
   (`pt_BR.json`, `zh_Hans.json`).
2. Translate it, and open a pull request.

No code to change: the build finds the new file by itself, and the name shown in Settings comes from
the language code.

## For developers

After adding or changing a `tr()` text:

```sh
cmake --build build --target update_translations
```

`lupdate` reads the code; then every `<code>.json` and `template.json` get the new texts (empty) and
lose the ones that are gone, while the others keep their translation. Run it only on code that
builds. Then translate the new texts in `it.json` (Italian must stay complete: a test checks it) and
commit the JSON files.

At build time, the `MentisTranslations` tool turns each JSON file into the `.ts` file that Qt's
`lrelease` compiles into the app, and lists the languages that reach 80%.
