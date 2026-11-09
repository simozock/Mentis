# README and manual media

`readme_media.cpp` renders the screenshots and animated GIFs of the repository's front page
(`media/`) and of the user manual (`manual/images/`) from the real Mentis window, on Qt's offscreen
platform. The maps are written with the same tools the agents use (`McpMapTools`), then opened in a
`MainWindow` and grabbed.

The tool uses its own settings (`MentisMedia/MentisReadmeMedia`) and Qt's test-mode folders, and
writes its maps to a temporary folder: your Mentis settings, recent maps and files are not touched.
The interface is English, dates are British English.

## Build

The target is built only with the CMake option `MENTIS_BUILD_MEDIA` (off by default):

```sh
cmake -S . -B build -DMENTIS_BUILD_MEDIA=ON
cmake --build build --target MentisReadmeMedia
```

## Run

From the repository root, with Qt's `bin` folder on `PATH` (Windows) and the offscreen platform.
The argument is the repository folder: the pictures go to its `media/` and `manual/images/`.

```sh
QT_QPA_PLATFORM=offscreen build/MentisReadmeMedia .
```

PowerShell:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
.\build\MentisReadmeMedia.exe .
```

A second argument renders only some of the pictures, comma-separated:
`map,dark,outliner,gantt,pitch,home,build,structures,themes,window,updates,basics,styling,contents,outliner-pages,gantt-pages,pitch-pages,files,faster,settings,help`.
With `MENTIS_MEDIA_DEBUG` set to a folder, single GIF frames are also saved there as PNG.

## Output in `media/` (front page)

| File | Content |
|---|---|
| `screenshot-map.png` | The Photosynthesis map: stickers, markers, checkboxes, a boundary, a summary, a relationship (1440x900) |
| `screenshot-map-dark.png` | The same map on a dark background (1440x900) |
| `screenshot-outliner.png` | The same sheet in the outliner (1440x900) |
| `screenshot-gantt.png` | A thesis plan in the Gantt view: tasks, dependencies, milestones (1440x900) |
| `screenshot-pitch.png` | A Pitch slide of the map, Playful theme, grid layout (1440x810) |
| `screenshot-home.png` | The Home with the recent maps (1440x900) |
| `demo-build-map.gif` | A map typed topic by topic (Tab, Enter) while the layout reflows (960x600) |
| `demo-structures.gif` | One map in seven map types, chosen from the Map page (960x600) |
| `demo-pitch-themes.gif` | Two slides of the map in seven Pitch themes (960x540) |

## Output in `manual/images/` (user manual)

| File | Scene | Content |
|---|---|---|
| `getting-started/window.png` | `window` | The editor with numbered marks on its parts (1440x900) |
| `getting-started/install-updates.png` | `updates` | Settings > General, scrolled to Updates |
| `getting-started/manual-window.png` | `help` | The manual's window, opened with "?" on a map |
| `map/topics.png` | `basics` | A selected topic with its "+" knobs and its bar |
| `map/collapse.png` | `basics` | A collapsed branch with its count |
| `map/relationships.png` | `basics` | The "powers" relationship |
| `map/boundaries.png` | `basics` | The "Raw materials" boundary |
| `map/summaries.png` | `basics` | The "Used in respiration" summary |
| `map/sheets.png` | `basics` | The sheet bar at the bottom left |
| `map/numbering.png` | `basics` | The map numbered 1.2.3 |
| `map/map-page.png` | `basics` | The Map page of the Format panel (1440x900) |
| `style/shapes.png` | `styling` | Twelve topics in twelve shapes |
| `style/style-page.png` | `styling` | The Style page for a selected topic (1440x900) |
| `style/style-page-lower.png` | `styling` | The Style page from Text down: Text, Structure, Branch, Numbering |
| `style/themes.gif` | `styling` | The example map in seven color themes (960x600) |
| `topic-contents/contents.png` | `contents` | A chemistry map with every kind of content (1440x900) |
| `topic-contents/notes.png` | `contents` | The note editor beside its topic (1440x900) |
| `topic-contents/labels.png`, `checkboxes.png`, `images.png`, `formulas.png`, `links-and-files.png` | `contents` | Crops of that map |
| `topic-contents/markers.png`, `stickers.png` | `contents` | The Marker and Sticker pages of the side panel (1440x900) |
| `topic-contents/sticker-editor.png` | `contents` | The sticker editor on a built-in sticker |
| `outliner/markdown-shortcuts.gif` | `outliner-pages` | Bold, a list and a highlight formed while typing a note |
| `outliner/paste-markdown.gif` | `outliner-pages` | A Markdown list pasted into the outliner, before and after (960x600) |
| `gantt/task-panel.png`, `gantt-style.png` | `gantt-pages` | The Task and Gantt pages of the Gantt side panel (1440x900) |
| `gantt/critical-path.png`, `baseline.png` | `gantt-pages` | The critical path; the plan compared with a snapshot (1440x900) |
| `gantt/resources.png` | `gantt-pages` | The Resources dialog with two people |
| `pitch/pitch-tab.png`, `template-editor.png` | `pitch-pages` | The Pitch tab for a branch; the template editor (1440x900) |
| `pitch/layouts.gif` | `pitch-pages` | The central topic's list slide in the ten layouts (960x540) |
| `files/export-image.png`, `export-pdf.png` | `files` | The Export image window; the Export PDF window with a frame (1440x900) |
| `map/search.png` | `faster` | The search panel with three results (1440x900) |
| `settings/shortcuts.png`, `settings-editor.png` | `faster` | Settings: the Shortcuts tab; the Editor tab at Writing |
| `settings/general.png`, `appearance.png`, `editor.png` | `settings` | Settings: the General, Appearance and Editor tabs |

A new manual picture is a function in `readme_media.cpp`, its scene name in `Media::run` and a row
here; the rules for pictures are in [manual/STYLE.md](../../manual/STYLE.md).

The GIFs are written by a small GIF89a encoder in the tool: one palette of the most frequent
colours, only the changed rectangle of each frame, endless loop.
