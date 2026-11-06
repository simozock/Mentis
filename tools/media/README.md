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

## Promotional videos

`promo_videos.cpp` (target `MentisPromoVideos`, same CMake option) records short videos for YouTube
and social media from the real window: MP4, H.264, 30 fps, AAC music. Each video exists in two
formats: landscape 1920x1080 (window 1280x720 at 1.5x) and portrait 1080x1920 for Shorts and Reels
(window 910x1190 between a caption band and a key band). The window is driven with real mouse and
key events; a drawn cursor, click ripples, key caps and captions are painted over the frames, and
each video opens and closes on a card with the icon. The portrait versions leave out a few steps.

Effects: the view stays on the whole window; a camera closes in only for work in one small area
(a topic or a row being written) and goes back out when a click, drag or menu happens outside what
it shows (`Stage::closeUp`, `Stage::keepOrWiden`); a mint glow and a ripple where the mouse clicks;
captions and key caps pop in; a light sweep crosses the window when a new part begins; the app fades
in from the opening card and shrinks under the closing one.

### Music

The music is chosen with `--music=…` (anywhere among the arguments); it always fades in on the
opening card and out on the closing one:

| Option | Sound track |
|---|---|
| `--music=builtin` (default) | The tune composed by the tool (class `Music`): a calm lo-fi loop in C major at 88 BPM, no third-party rights |
| `--music=none` | None: the videos have no audio track (to add music later in an editor or on the platform) |
| `--music=<file>` | A sound file (MP3, WAV, M4A…: what Qt's FFmpeg backend reads), converted to 48 kHz stereo and looped when shorter than the video |

```powershell
.\build\MentisPromoVideos.exe "$env:USERPROFILE\Downloads\video mentis" --music="C:\Music\my track.mp3"
```

Before using a track from a library, check that its licence allows YouTube and Instagram without
attribution or, if it asks for one, put the credit in the video's description. `music` as second
argument writes 40 s of the built-in tune alone as `Mentis - music.wav`.

A new source is a class derived from `MusicSource` (one method, `render`) and a branch for its name
in `main`.

### Encoding

Frames and sound are encoded with Qt Multimedia (`QVideoFrameInput`, `QAudioBufferInput` and
`QMediaRecorder`, FFmpeg backend; files read with `QAudioDecoder`): no external encoder. Settings are isolated as for the pictures; tours and tips are switched off.

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
.\build\MentisPromoVideos.exe "$env:USERPROFILE\Downloads\video mentis"
```

The second argument picks videos: `map`, `outliner`, `gantt`, `export`, each optionally with `-h`
(landscape) or `-v` (portrait), comma-separated. With `MENTIS_MEDIA_DEBUG` set to a folder, one frame
a second is saved there as JPEG.

| File | Content |
|---|---|
| `Mentis - Mind map basics (…).mp4` | Topics with Tab and Enter, relationship, boundary, markers and a sticker, map types, colour styles, folding |
| `Mentis - Outliner (…).mp4` | Rows, levels, Markdown while typing, a checkbox, a note, folding, a pasted Markdown list, back to the map |
| `Mentis - Gantt basics (…).mp4` | The plan from the map, moving and stretching bars, a link, progress, critical path, a snapshot compared, side panel, people, weeks |
| `Mentis - Export (…).mp4` | Main menu › Export: image with a frame, PDF of map, outline and slides, Word, Excel, PowerPoint, Markdown |
