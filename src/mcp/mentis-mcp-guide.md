# Mentis maps for agents

Mentis is a mind map app. This server lets you read and edit Mentis maps (`.mentis` files) for the
user. The user can turn the server off in Mentis (Settings > General > Agents (MCP)). Paths are
absolute; work only on the files the user asked for.

## The model

- A map file has one or more **sheets**; each sheet is a mind map with one **central topic**.
- Every topic has an `id`, a `title`, optional `notes` and `labels`, and ordered `children`.
- Topics are addressed by `id`. Ids are stable; get them from `read_map`, or from the answer of
  `add_topic` and `import_markdown`. A `sheet` argument is a sheet id; omitted = the
  first sheet of a file on disk, the sheet the user is looking at in a window.

## How to work

1. **The map the user has open**: call the map tools without `path`. They work on the map in the
   Mentis window used last, also one without a name; the user sees every change at once and can
   undo it (Ctrl+Z). `save_open_map` saves it (with `path`: a new file).
2. **Maps on disk**: `list_maps` shows the maps the user opened recently, or those in a `folder`.
   With `path`, a map open in a window is changed in that window, any other on disk. A map you
   made with `create_map` stays on disk: offer to open it, then `open_map` (Mentis asks the user;
   with Mentis closed pass `confirm: true` only after the user said yes in the chat).
3. Always `read_map` before changing a map, and again after big changes.
4. Prefer `import_markdown` (or `create_map` / `add_sheet` with `markdown`) for many topics at
   once: headings and nested list items become the branches.
5. Titles are plain text and short (a few words); put longer text in `notes`.
6. `delete_topic` removes a whole branch. Delete only what the user asked to remove.
7. Mentis lays the map out after every change: do not try to place topics.
8. Style and contents: `set_style` (colours, shape, font, branch, presets such as very important),
   `reset_style`, `set_marker`, `set_sticker`, `set_image`, `set_formula`, `set_links`, `set_task`,
   `attach_files`. Image, sticker and formula share the picture place: `placement` (above, below,
   left, right of the title) and `width` in pixels, also later with `place_visual`; `read_map`
   shows them as `picture`. Call `list_options` once for the accepted values (shapes, markers,
   stickers).
   Use style sparingly and consistently: it should help the user read the map.
9. Elements: `add_relationship` (a line between two topics, with a label), `add_summary` and
   `add_boundary` (over siblings; a balanced map gets one per side), `add_floating_topic`,
   `update_element`, `update_relationship` (other ends, curve handles as [x, y] from each end's
   centre, colour, width, solid/dashed/dotted, end marks, label font; null = automatic or default),
   `delete_elements`. `read_map` lists them with their ids and settings.
10. Sheets and structure: `duplicate_sheet`, `move_sheet`, `delete_sheet`, `set_structure` (map
    type and direction, list_options lists them), `set_balanced`, `auto_layout`, `set_collapsed`.
11. **Confirmation**: deleting a sheet (and replacing a file) needs the user's explicit yes. When
    Mentis is open it asks the user itself; otherwise ask the user in the chat and pass
    `confirm: true` only if they agree. Never pass it on your own.
12. Gantt (every topic but the central one is a task, with the topic's id): `read_gantt`,
    `set_gantt_task` (start YYYY-MM-DD, duration in working days, progress, priority),
    `add_dependency`, `remove_dependency`, `add_person`, `update_person` (name, role, e-mail, hourly
    cost), `remove_person` (one assigned to tasks: the user's yes), `assign_people` (with `percent`,
    each person's share of the task), `set_currency`, `list_currencies`, `add_currency` and
    `remove_currency` (currencies saved on this computer), `set_calendar` (working weekdays,
    holidays, hours per day), `add_baseline`, `rename_baseline`, `set_gantt_style` (shape, fill,
    border and colour of tasks, summaries and milestones; line, end, width of the arrows),
    `set_gantt_task_style` (a task's own shape, fill, border, colour, name on the bar).
    `read_gantt` also lists shares, own styles, the chart style, the baselines, the calendar and the
    currency. Summaries take their dates from their tasks.
13. Exports and presentation: `export_map` (.png, .svg, .pdf, .docx, .xlsx, .md to a
    file), `set_pitch` (theme or template, aspect, per-topic slides). Replacing an existing file, also with
    `create_map` or `save_open_map`, needs the user's yes as above.
14. Sticker and marker library (the same for every map; `kind`: sticker or marker): `list_library`,
    `add_to_library` (an SVG you write, or a file), `create_collection`,
    `rename_collection`, `move_collection`, `move_library_item`, `rename_library_item` (own items),
    `remove_library_item`,
    `remove_collection` (own ones: the user's yes; built-in ones are only hidden), `restore_builtins`.
    Then `set_sticker` / `set_marker` take the item's path. Keep SVGs simple, square, readable small.
15. **Windows of Mentis**: `open_dialog` opens one for the user when they ask (export image or PDF
    with a frame, background, scale..., Word, Excel, Pitch, Gantt resources, the sticker editor on a
    sticker, a new palette, formula / links / labels / note of a topic, settings on a page, the
    presentation). It returns once the window is open: the user completes it; you cannot press its
    buttons. Example: `add_to_library` with your SVG, then `open_dialog` `sticker_editor` on it.
16. Colours and text: `list_palettes`, `create_palette` (six colours, for every map),
    `update_palette` (name, colours), `delete_palette` (the user's yes), `set_theme` (palette, variant 0-5 with its background, a free
    background), `set_sheet_style`. Formatted text: `title_markdown` and `notes_markdown` in
    `add_topic` / `update_topic`. In the whole map: `rename_label`, `find_replace` (`in`: titles,
    notes, labels; formatting stays). Attached files: `rename_file` (index as read_map lists them).
17. Pitch templates (own presentation themes, for every map): `list_pitch_templates`,
    `save_pitch_template` (new, or `id` to change: base theme, colours, fonts, item style, motif,
    background pictures and logo from files, footer, slide numbers),
    `import_pitch_template` / `export_pitch_template` (.mentis-theme files), `delete_pitch_template`
    (the user's yes). `set_pitch` with `template` applies one to a sheet; a theme removes it.

## Limits

- Title or notes: 10,000 characters. Markdown: 1 MB and 5,000 items per call.
- `read_map` returns up to 5,000 topics (`truncated` tells when there are more).

## Bugs

If Mentis behaves wrongly (an error that should not happen, a wrong result), call
`report_issue`. It sends nothing: show the user the text and the link it returns, and let the user
decide whether to open the issue. Do not include the content of the user's maps unless the user
agrees.
