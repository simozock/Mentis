<p align="center">
  <img src="assets/appicon/mentis-128.png" alt="Mentis" width="72">
</p>

<h1 align="center">Changelog</h1>

<p align="center"><a href="README.md">← Back to the README</a></p>

All notable changes to Mentis are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org).

## [Unreleased]

### Security

- Installers of new releases come with GitHub build provenance attestations, so you can check
  they were built from this repository (`gh attestation verify <file> --repo simozock/Mentis`).
- A map received from someone else can no longer write files outside Mentis's own folders
  through crafted attachment names or image ids, nor open `file:` or network-share links: only
  web and mail links open. Attachments are opened and exported as `attach01.pdf`,
  `attach02.docx`... while the map keeps their names. More file types (such as `.dll`, `.url`, disk images) ask before
  opening.
- A received map that contains database triggers or views is refused as not valid: their SQL
  could change the map behind your back each time you saved.
- Titles and notes no longer load images or style sheets from files or network shares, which on
  Windows could send your credentials to another computer just by opening a map. Images pasted
  into a note are now embedded in it, so they stay in the map and show on any computer.

### Fixed

- Mentis could crash when a topic had many children (about 100), at opening the map or
  in the presentation preview.
- A damaged or crafted map with a loop between topics, a repeated topic or more than 500 levels
  is refused instead of crashing the app. Adding or indenting topics past 500 levels is stopped
  with a message that suggests reorganising the map.
- Found by the new fuzzing tests: a crafted map with repeated topic ids crashed the app on Undo,
  and one with tables nested very deep in a title or a note froze it; both are handled now.
- The update check no longer fails with "Could not check for updates" when GitHub's API limit
  for the network is used up: it reads the latest release from github.com instead.

## [0.8.1] - 2026-11-06

### Added

- Settings > General > Updates shows the date and time of the last update check.

### Fixed

- Windows: after an update the desktop shortcut shows the new icon.

## [0.8.0] - 2026-11-05

### Added

- Summaries: the summary topic takes subtopics (its "+" or Tab), a branch that grows away from the
  bracket; the topics around the summary make room for it.
- Paste on the map: Ctrl+V on the selected topic adds an image, files or a web link to it, also
  while its title is being typed; copied branches (several at once too) go under the selected topic.
- Markdown pasted in the Outliner goes under the item.
- Typing on a selected topic replaces its title; Space and F2 open the title with the cursor at the end.
- Text formatting keys on a topic or the selected words: Ctrl+B, Ctrl+I, Ctrl+U, Ctrl+Shift+X.
- Checkbox: Ctrl+Alt+T, the checkbox button and the "+" menus turn it on and off.
- Ctrl+Alt+S and Ctrl+Alt+M show and hide the side panels; every General command can have its own
  key, F1 included.
- Numbering: an option to end the number with the separator (1.2.3. or 1.2.3).
- Stickers and markers: search by name (English names, also for translated ones); a built-in
  "Mentis" collection with the mascot's 13 drawings.
- Releases: a macOS disk image, `Mentis-<version>-macos.dmg` (universal, not notarized yet).

### Changed

- A new mascot, the brain with glasses, in the tour and the stickers; new app icon.
- The tour: shorter steps with arrows on what to click; the window stays usable while it runs.
- Faster startup (Home on screen in about half a second) and faster closing.
- The map follows the side panel as it opens and shows new topics placed at its edge.
- Side panel: unavailable controls look clearly disabled.
- Unsaved changes dialog: Don't save on the left, Save on the right, X to cancel.

### Fixed

- A summary topic showed two "+" buttons that did nothing.
- Ctrl+C and Ctrl+X copy and cut the selected branch on the map.
- The window can be moved from the Home and while a dialog is open.
- A longer title pushes the topic's children at once, and Ctrl+Z puts the topic back.
- Space and F2 open the selected topic's title again after Enter.
- A sticker deleted from the library no longer stops the map from saving.
- The help button of dialogs is white again.

## [0.7.0] - 2026-11-03

First public source code: mind maps with many structures, outliner, notes, labels, tasks, images,
audio, formulas, links, attachments and SVG stickers; Gantt plans; Pitch presentations; Markdown
import; Markdown, PNG, SVG, PDF, Word and Excel export; MCP server; English and Italian interface.

- Updates: a daily check of the GitHub releases at startup (Settings > General > Updates, with
  Check now). On Windows the per-user installer downloads, is verified against `SHA256SUMS` and
  installs after a restart; on macOS and Linux the release page opens.
