# How the manual is written

Short, schematic pages, one per feature: a sentence, a picture, the tasks. New pages start from
[TEMPLATE.md](TEMPLATE.md).

## Structure

- One folder per **area** of the app, each with a `README.md`: `getting-started`, `map`,
  `topic-contents`, `style`, `outliner`, `gantt`, `pitch`, `files`, `settings`, `ai-agents`,
  `questions`. Two levels only: `manual/<area>/<page>.md`.
- File names are lowercase words joined by hyphens, as the feature is called in the app.
- [README.md](README.md) lists every area and each area's `README.md` lists its pages, as numbered
  items `1. **[Title](page.md)** — a few words`: the manual window builds its contents from them.
- A page describes only what Mentis does today.

## A page

1. **Breadcrumb**: `[Manual](../README.md) › [Area](README.md) › Page`.
2. **Title** (`#`): the feature's name as in the app.
3. **One sentence**: what it is for.
4. **A picture** of the result.
5. **Tasks** (`##`), one per thing the reader wants to do, named with a verb: "Add a topic".

A page is 80–250 words. Longer means two pages.

## Keep it short

- A task is 1–4 numbered steps or a short list. Steps start with a verb.
- Give the main way first; other ways go on one line: "Or: right-click › **Copy**."
- The shortcut stays beside its action. No separate shortcut table, no macOS column: macOS uses
  <kbd>⌘</kbd> for <kbd>Ctrl</kbd>, as said once in [Keyboard shortcuts](settings/keyboard-shortcuts.md).
- One table at most, for options (**Option** | what it does, in one line).
- One callout at most (`> [!NOTE]`, `> [!TIP]` or `> [!WARNING]`), for a rule the reader must know.
- No "Related" list and no previous/next links: the index, Back and Forward do that. Link another
  page only where the reader needs it.
- Say each thing once. No introductions, no summaries, no "you can also".

## Words

- Write to the reader, present tense, everyday words.
- Use the app's English labels in bold: **Add child**, **Format panel**.
- A *topic* is a box of the map; the *central topic* is the main one.

## Formatting

| What | How |
|---|---|
| Buttons, menus, fields, tabs | bold, as in the app: **Insert content** › **Note** |
| Menu paths | bold items joined by ›: **Main menu** › **Export** › **PDF…** |
| Keys | `<kbd>`: <kbd>Ctrl</kbd>+<kbd>S</kbd> |
| File names, typed text | code: `Notes.mentis` |

## Pictures and GIFs

- Every picture is made by the app: `tools/media/readme_media.cpp` opens the real window, builds the
  map with the agents' tools and grabs it (see [tools/media/README.md](../tools/media/README.md)).
- They live in `manual/images/<area>/<page>.png`; a page may show a picture from `media/`.
- Light theme, English interface, the tool's example maps. PNG under 300 KB, GIF under 1 MB.
- The alternative text says what the picture shows.

## Checking a page

- Every link and picture path opens.
- The steps work in the current version, with the labels as the app shows them.
