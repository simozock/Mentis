cmake_minimum_required(VERSION 3.24)

project(Mentis VERSION 0.8.1 LANGUAGES CXX)

# 2026-10-14: senza un tipo di build (generatori a configurazione singola, es. Ninja da riga di
# comando) il compilatore non ottimizzava (-O0). Predefinito Release; Qt Creator e chi passa
# -DCMAKE_BUILD_TYPE scelgono come prima.
# https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo di build" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(MENTIS_BUILD_BENCHMARKS "Compila i benchmark riproducibili di Mentis" OFF)
option(MENTIS_BUILD_MEDIA "Build the tool that renders the README screenshots and GIFs (tools/media)" OFF)

# Svg: QSvgGenerator for the SVG export; PrintSupport: printing (FEAT-054, both approved by the user
# 2026-10-26; Qt modules, same license). Qt 6.8 for QPdfWriter::setColorModel (grey PDF).
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Sql Concurrent Svg PrintSupport Network)
# Qt 6.8's OpenGL wrapper links the AGL framework on macOS, which recent Xcode SDKs no longer ship:
# the link fails with "ld: framework 'AGL' not found" (GitHub's macos-latest, 2026-11-02). Mentis
# uses no OpenGL of its own, so AGL is dropped from the wrapper's link libraries.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(mentis_gl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(mentis_gl_libraries)
        list(FILTER mentis_gl_libraries EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${mentis_gl_libraries}")
    endif()
endif()

# Renderer di formule LaTeX (JKQTMathText, LGPL 2.1): libreria condivisa separata.
# La DLL/.so viene messa nella stessa cartella dell'eseguibile, così l'app la trova
# sia avviata da Qt Creator sia da riga di comando.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_subdirectory(third_party/jkqtmathtext)

# 2026-10-15 (build piu' veloce, richiesta dell'utente): i sorgenti dell'app si compilano una volta
# sola in una libreria statica interna, usata dall'app e da tutti i test. Prima ogni test con
# l'interfaccia ricompilava da capo tutto src/ (mainwindow.cpp 5 volte, mindmapmodel.cpp 8).
qt_add_library(MentisCore STATIC
    src/boundary.h
    src/boundary.cpp
    src/relationship.h
    src/relationship.cpp
    src/summary.h
    src/summary.cpp
    src/numbering.h
    src/numbering.cpp
    src/ganttplan.h
    src/ganttplan.cpp
    src/ganttcosts.h
    src/ganttcosts.cpp
    src/ganttresources.h
    src/ganttresources.cpp
    src/resourcesdialog.h
    src/resourcesdialog.cpp
    src/ganttview.h
    src/ganttview.cpp
    src/datepicker.h
    src/datepicker.cpp
    src/numberfield.h
    src/numberfield.cpp
    src/gantttaskpanel.h
    src/gantttaskpanel.cpp
    src/ganttbaselinemenu.h
    src/ganttbaselinemenu.cpp
    src/ganttexport.h
    src/ganttexport.cpp
    src/ganttoffice.h
    src/ganttoffice.cpp
    src/officeexportdialog.h
    src/officeexportdialog.cpp
    src/exportframe.h
    src/exportframe.cpp
    src/exportframecontrols.h
    src/exportframecontrols.cpp
    src/selectioncommands.h
    src/selectioncommands.cpp
    src/boundaryitem.h
    src/boundaryitem.cpp
    src/relationshipitem.h
    src/relationshipitem.cpp
    src/formulatext.h
    src/formulatext.cpp
    src/markdownimport.h
    src/markdownimport.cpp
    src/localization.h
    src/localization.cpp
    src/document.h
    src/document.cpp
    src/shortcuts.h
    src/shortcuts.cpp
    src/uitheme.h
    src/uitheme.cpp
    src/windowframe.h
    src/windowframe.cpp
    src/mcp/mcpserver.h
    src/mcp/mcpserver.cpp
    src/mcp/mcpmaptools.h
    src/mcp/mcpmaptools.cpp
    src/mcp/mcpstdio.h
    src/mcp/mcpstdio.cpp
    src/mcp/mcpbridge.h
    src/mcp/mcpbridge.cpp
    src/mcp/mcpclients.h
    src/mcp/mcpclients.cpp
    src/mcp/mcptoolsupport.h
    src/mcp/mcpcontenttools.cpp
    src/mcp/mcpelementtools.cpp
    src/mcp/mcpsheettools.cpp
    src/mcp/mcpgantttools.cpp
    src/mcp/mcpexporttools.cpp
    src/mcp/mcplibrarytools.cpp
    src/mcp/mcpthemetools.cpp
    src/mcp/mcppitchtemplatetools.cpp
    src/latexrenderer.h
    src/latexrenderer.cpp
    src/maphistory.h
    src/maphistory.cpp
    src/bmapstorage.h
    src/bmapstorage.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mindmapmodel.h
    src/mindmapmodel.cpp
    src/nodestyle.h
    src/nodeshape.cpp
    src/nodesketch.cpp
    src/richtitle.cpp
    src/richresources.h
    src/richresources.cpp
    src/nodestyle.cpp
    src/mindmapscene.h
    src/mindmapscene.cpp
    src/mindmapview.h
    src/mindmapview.cpp
    src/nodeitem.h
    src/nodeitem.cpp
    src/texteditmenu.h
    src/texteditmenu.cpp
    src/spellchecker.h
    src/spellchecker.cpp
    src/topictools.h
    src/topictools.cpp
    src/stylecontrols.h
    src/stylecontrols.cpp
    src/settingsdialog.h
    src/settingsdialog.cpp
    src/toggleswitch.h
    src/custompalettes.h
    src/filevalidation.h
    src/noteeditor.h
    src/noteeditor.cpp
    src/markdownshortcuts.h
    src/markdownshortcuts.cpp
    src/formuladialog.h
    src/formuladialog.cpp
    src/markers.h
    src/markers.cpp
    src/mapstructure.h
    src/mapstructure.cpp
    src/mapvariants.h
    src/mapvariants.cpp
    src/variantgallery.h
    src/variantgallery.cpp
    src/nodesearch.h
    src/nodesearch.cpp
    src/wheelguard.h
    src/wheelguard.cpp
    src/updates.h
    src/updates.cpp
    src/updateprompt.h
    src/updateprompt.cpp
    src/help.h
    src/help.cpp
    src/manualwindow.h
    src/manualwindow.cpp
    src/tour.h
    src/tour.cpp
    src/navigationpanel.h
    src/navigationpanel.cpp
    src/branchpath.h
    src/branchpath.cpp
    src/structurepicker.h
    src/structurepicker.cpp
    src/iconlibrary.h
    src/iconlibrary.cpp
    src/iconpanel.h
    src/iconpanel.cpp
    src/stickereditor.cpp
    src/svgdocument.cpp
    src/svgshapes.cpp
    src/stickers.h
    src/stickers.cpp
    src/labeleditor.h
    src/labeleditor.cpp
    src/filevalidation.cpp
    src/custompalettes.cpp
    src/palettedialog.h
    src/palettedialog.cpp
    src/toggleswitch.cpp
    src/floatingui.h
    src/floatingui.cpp
    src/performance.h
    src/performance.cpp
    src/segmentedcontrol.h
    src/segmentedcontrol.cpp
    src/sidepanel.h
    src/sidepanel.cpp
    src/panelcontext.h
    src/panelcontext.cpp
    src/nodelinks.h
    src/nodelinks.cpp
    src/clipboardpaste.h
    src/clipboardpaste.cpp
    src/modaldialog.h
    src/modaldialog.cpp
    src/linkdialogs.h
    src/linkdialogs.cpp
    src/mapexporter.h
    src/mapexporter.cpp
    src/exportdialog.h
    src/exportdialog.cpp
    src/pagedialog.h
    src/pagedialog.cpp
    src/pitch.h
    src/pitch.cpp
    src/pitchpage.h
    src/pitchpage.cpp
    src/pitchwindow.h
    src/pitchwindow.cpp
    src/pitchexport.h
    src/pitchexport.cpp
    src/pitchtemplate.h
    src/pitchtemplate.cpp
    src/pitchtemplateeditor.h
    src/pitchtemplateeditor.cpp
    src/zipwriter.h
    src/zipwriter.cpp
    src/ooxml.h
    src/ooxml.cpp
    src/officeexport.h
    src/officeexport.cpp
)
target_include_directories(MentisCore PUBLIC src)
# Qt Network: the local channel between "Mentis --mcp" and the open windows (FEAT-045 phase 2) and
# the update check against GitHub (FEAT-090).
target_link_libraries(MentisCore PUBLIC Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Svg Qt6::PrintSupport Qt6::Network MentisMathText)
# FEAT-090: the version has one source, project(Mentis VERSION ...) above. Only updates.cpp reads it
# (Updates::currentVersion): a new version recompiles one file, not the whole library.
set_source_files_properties(src/updates.cpp PROPERTIES COMPILE_DEFINITIONS MENTIS_VERSION="${PROJECT_VERSION}")
# FEAT-044: system spell checker. Windows: Spell Checking API (COM, ole32); macOS and Linux later.
if(WIN32)
    target_sources(MentisCore PRIVATE src/spellchecker_win.cpp)
    target_link_libraries(MentisCore PUBLIC ole32)
endif()
# FEAT-071: Snap and resizing of the app-drawn window on Windows (DWM corners, auto-hide taskbar).
if(WIN32)
    target_sources(MentisCore PRIVATE src/windowframe_win.cpp)
    target_link_libraries(MentisCore PUBLIC dwmapi shell32)
endif()

# Icons and stickers (resources.qrc, about 12 MB of generated code) compiled once, in an OBJECT
# library linked by the app and the UI tests (2026-10-23: before, every test executable compiled them
# again and the build was slow). Its objects go into each executable, so the resources register
# themselves without Q_INIT_RESOURCE.
add_library(MentisResources OBJECT resources.qrc)
set_target_properties(MentisResources PROPERTIES AUTORCC ON)
target_link_libraries(MentisResources PRIVATE Qt6::Core)
# FEAT-092: the user manual inside the app, read by the manual window from :/docs/manual/...: its
# pages, its pictures and the front page's pictures it shows (media/), at the same relative paths.
file(GLOB_RECURSE mentis_manual_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/manual/*.md" "${CMAKE_CURRENT_SOURCE_DIR}/manual/images/*")
file(GLOB mentis_manual_media CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/media/screenshot-*.png" "${CMAKE_CURRENT_SOURCE_DIR}/media/demo-*.gif")
qt_add_resources(MentisResources mentis_manual PREFIX "/docs" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${mentis_manual_files} ${mentis_manual_media})

qt_add_executable(Mentis
    src/main.cpp
)

target_link_libraries(Mentis PRIVATE MentisCore MentisResources)

# Compiler warnings for the app's own code (2026-11-09, user's request), not third_party: -Wall
# -Wextra on GCC and Clang, without -Wmissing-field-initializers (aggregates that leave fields at
# their defaults on purpose: 246 of the 258 warnings measured). MSVC keeps its default level.
# MENTIS_WARNINGS_AS_ERRORS (CI on Linux) makes a new warning fail the build.
option(MENTIS_WARNINGS_AS_ERRORS "Treat the app's compiler warnings as errors (GCC, Clang)" OFF)
function(mentis_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers
                               $<$<BOOL:${MENTIS_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()
mentis_warnings(MentisCore)
mentis_warnings(Mentis)

# FEAT-045 / ADR-005: the guide for agents is the MCP server's instructions.
qt_add_resources(MentisCore "mcpguide" PREFIX "/mcp" BASE "src/mcp" FILES src/mcp/mentis-mcp-guide.md)
target_include_directories(MentisCore PUBLIC src/mcp)

# FEAT-046 / ADR-003: the code's texts are English; every other language is a translation compiled
# into the app (resources :/i18n). FEAT-091 (user, 2026-11-02): the translations are the files
# translations/<code>.json, one per language, grouped by Qt context, edited by hand by volunteers
# and sent as pull requests. The tool MentisTranslations turns them into the .ts files that lrelease compiles,
# and "cmake --build build --target update_translations" adds the code's new texts to them.
# https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-lupdate.html
# https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-lrelease.html
find_package(Qt6 6.7 REQUIRED COMPONENTS LinguistTools)
qt_add_executable(MentisTranslations
    tools/translations/main.cpp
    tools/translations/translationfiles.h
    tools/translations/translationfiles.cpp
)
target_link_libraries(MentisTranslations PRIVATE Qt6::Core)
# A folder of its own: on Windows the build runs the tool with a copy of Qt Core beside it, whatever
# the PATH. In the build folder that copy made the app and the tests load Qt from there and miss
# its plugins ("no Qt platform plugin could be initialized", 2026-11-02).
set_target_properties(MentisTranslations PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/translations")
if(WIN32)
    add_custom_command(TARGET MentisTranslations POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:MentisTranslations>)
endif()

set(MENTIS_TRANSLATION_THRESHOLD 80)  # percent translated for a language to be offered
file(GLOB mentis_language_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.json")
list(FILTER mentis_language_files EXCLUDE REGEX "/template\.json$")
list(SORT mentis_language_files)

# Every text of the code, read by lupdate into the build folder; update_translations merges it
# into the JSON files. -nounfinished at lrelease: an empty value shows in English.
set(mentis_lupdate_ts "${CMAKE_BINARY_DIR}/translations/lupdate/mentis.ts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/translations/lupdate")  # lupdate does not create it
qt_add_lupdate(SOURCE_TARGETS MentisCore Mentis
    TS_FILES "${mentis_lupdate_ts}"
    LUPDATE_TARGET Mentis_lupdate
    NO_GLOBAL_TARGET  # update_translations is ours: lupdate, then the merge into the JSON files
    OPTIONS -locations none -no-obsolete
)
add_custom_target(update_translations
    COMMAND MentisTranslations sync "${mentis_lupdate_ts}" "${CMAKE_CURRENT_SOURCE_DIR}/translations"
    COMMENT "Adding the code's texts to translations/*.json"
    VERBATIM)
add_dependencies(update_translations Mentis_lupdate MentisTranslations)

set(mentis_generated_ts)
foreach(json IN LISTS mentis_language_files)
    get_filename_component(code "${json}" NAME_WE)
    set(ts "${CMAKE_BINARY_DIR}/translations/mentis_${code}.ts")
    add_custom_command(OUTPUT "${ts}"
        COMMAND MentisTranslations ts "${json}" "${ts}" "${code}"
        DEPENDS "${json}" MentisTranslations
        COMMENT "Translation ${code}"
        VERBATIM)
    list(APPEND mentis_generated_ts "${ts}")
endforeach()
qt_add_lrelease(TARGETS Mentis
    TS_FILES ${mentis_generated_ts}
    OPTIONS -nounfinished
    QM_FILES_OUTPUT_VARIABLE mentis_qm_files)
qt_add_resources(Mentis mentis_translations PREFIX "/i18n" BASE "${CMAKE_BINARY_DIR}" FILES ${mentis_qm_files})

# FEAT-046: the languages offered in Settings go into a generated header, rewritten when a JSON
# file changes.
set(mentis_offered_header "${CMAKE_BINARY_DIR}/generated/offeredtranslations.h")
add_custom_command(OUTPUT "${mentis_offered_header}"
    COMMAND MentisTranslations offered ${MENTIS_TRANSLATION_THRESHOLD} "${mentis_offered_header}" ${mentis_language_files}
    DEPENDS ${mentis_language_files} MentisTranslations
    COMMENT "Offered languages"
    VERBATIM)
target_sources(MentisCore PRIVATE "${mentis_offered_header}")
target_include_directories(MentisCore PRIVATE "${CMAKE_BINARY_DIR}/generated")

# The app's icon (user, 2026-10-31): the .exe's from a resource script, the bundle's from the .icns.
# The script also carries the version information, from project() (FEAT-090).
if(WIN32)
    set_target_properties(Mentis PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(assets/appicon/mentis.rc.in "${CMAKE_BINARY_DIR}/generated/mentis.rc" @ONLY)
    target_sources(Mentis PRIVATE "${CMAKE_BINARY_DIR}/generated/mentis.rc")
elseif(APPLE)
    set_target_properties(Mentis PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_ICON_FILE mentis.icns)
    set_source_files_properties(assets/appicon/mentis.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(Mentis PRIVATE assets/appicon/mentis.icns)
endif()

# libFuzzer (2026-11-09): one executable per target of tests/fuzz/fuzztargets.h, with AddressSanitizer
# and UndefinedBehaviorSanitizer; clang only (CI on Linux, job "fuzz"). MentisCore gets the same
# instrumentation so that its code is explored and checked too.
option(MENTIS_BUILD_FUZZERS "Build the libFuzzer targets (clang)" OFF)
if(MENTIS_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MENTIS_BUILD_FUZZERS needs clang")
    endif()
    target_compile_options(MentisCore PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    foreach(mentis_fuzz_target markdown richText mapJson document)
        set(mentis_fuzzer MentisFuzz_${mentis_fuzz_target})
        qt_add_executable(${mentis_fuzzer} tests/fuzz/libfuzzer.cpp tests/fuzz/fuzztargets.cpp)
        target_link_libraries(${mentis_fuzzer} PRIVATE MentisCore)
        if(mentis_fuzz_target STREQUAL "document")
            target_compile_definitions(${mentis_fuzzer} PRIVATE MENTIS_FUZZ_DOCUMENT=1)
        else()
            target_compile_definitions(${mentis_fuzzer} PRIVATE MENTIS_FUZZ_TARGET=${mentis_fuzz_target})
        endif()
        target_compile_options(${mentis_fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(${mentis_fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

if(MENTIS_BUILD_BENCHMARKS)
    qt_add_executable(MentisBenchmark tests/performance_benchmark.cpp)
    target_link_libraries(MentisBenchmark PRIVATE MentisCore)
endif()

# The README's screenshots and GIFs from the real window on the offscreen platform (tools/media).
if(MENTIS_BUILD_MEDIA)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(MentisReadmeMedia tools/media/readme_media.cpp)
    target_link_libraries(MentisReadmeMedia PRIVATE MentisCore MentisResources Qt6::Test)
    # Promotional videos (MP4): Qt Multimedia encodes the frames, no external encoder. The tool is
    # not published on GitHub (tools/media/PROMO.md): only where its file is.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/media/promo_videos.cpp")
        find_package(Qt6 REQUIRED COMPONENTS Multimedia)
        qt_add_executable(MentisPromoVideos tools/media/promo_videos.cpp)
        target_link_libraries(MentisPromoVideos PRIVATE MentisCore MentisResources Qt6::Test Qt6::Multimedia)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS Mentis MentisMathText
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# The Qt libraries and plugins next to the installed app (cmake --install). "Mentis --mcp" runs on
# the offscreen platform, which the app's own plugins do not include (FEAT-045).
# OFF for the AppImage: linuxdeploy collects Qt and the system libraries itself (release.yml).
option(MENTIS_DEPLOY_QT "Copy the Qt libraries and plugins at install" ON)
if(MENTIS_DEPLOY_QT)
    # windeployqt only (macdeployqt takes other options). Mentis paints without OpenGL or Direct3D
    # (no QOpenGL, QRhi or Qt Quick): the software OpenGL (20 MB) and the D3D compiler (4 MB) stay
    # out; Qt's own translations only for the app's languages (all of them were 5.8 MB).
    # https://doc.qt.io/qt-6/windows-deployment.html
    set(mentis_deploy_options)
    if(WIN32)
        set(mentis_deploy_options DEPLOY_TOOL_OPTIONS --include-plugins qoffscreen --no-opengl-sw
            --no-system-d3d-compiler --translations en,it,de,es,fr)
    endif()
    qt_generate_deploy_app_script(TARGET Mentis OUTPUT_SCRIPT mentis_deploy_script NO_UNSUPPORTED_PLATFORM_ERROR
        ${mentis_deploy_options})
    install(SCRIPT ${mentis_deploy_script})
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(MentisStorageTest tests/storage_smoke.cpp)
    target_link_libraries(MentisStorageTest PRIVATE MentisCore)
    add_test(NAME MentisStorageRoundTrip COMMAND MentisStorageTest)
    if(WIN32)
        set_tests_properties(MentisStorageRoundTrip PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # Test UI con il vero MainWindow (piattaforma offscreen): usano la libreria dell'app e le icone.
    find_package(Qt6 REQUIRED COMPONENTS Test)
    # BUG-001/BUG-002: selezione nei campi dell'Outliner. FEAT-017: pannello laterale contestuale.
    # FEAT-018: larghezza automatica e manuale dei nodi.
    # FEAT-019: selettore colori unico e pagina Stile. BUG-013: disposizione con nodi spostati.
    # FEAT-027: collegamenti (menu, modali, icone, fogli). FEAT-037: immagine dentro il nodo.
    # BUG-014: Importa Markdown chiede di salvare le modifiche come Apri.
    # FEAT-066: regressione di file, fogli, storico e salvataggio automatico (DocumentFlow).
    # FEAT-044: controllo ortografico con un correttore finto e quello di Windows (SpellCheck).
    foreach(ui_test IN ITEMS OutlinerSelection SidePanel NodeWidth ColorPicker StylePage MapLayout NodeLinksUi NodeContent MarkdownImport DocumentFlow NodeOperations ShortcutsUi ImageStyle CustomPalettes ThemeVariants FileValidation TaskBox Notes Labels MarkdownTyping NodeFiles Formula StickersMarkers BalancedLayout NodeSearch SpellCheck WheelGuard Export Pitch PitchTemplate OfficeExport FocusMode Updates Help Tour)
        if(ui_test STREQUAL "OutlinerSelection")
            set(ui_test_source tests/outliner_selection_test.cpp)
        elseif(ui_test STREQUAL "SidePanel")
            set(ui_test_source tests/side_panel_test.cpp)
        elseif(ui_test STREQUAL "NodeWidth")
            set(ui_test_source tests/node_width_test.cpp)
        elseif(ui_test STREQUAL "ColorPicker")
            set(ui_test_source tests/color_picker_test.cpp)
        elseif(ui_test STREQUAL "BalancedLayout")
            set(ui_test_source tests/balanced_layout_test.cpp)
        elseif(ui_test STREQUAL "MapLayout")
            set(ui_test_source tests/map_layout_test.cpp)
        elseif(ui_test STREQUAL "NodeLinksUi")
            set(ui_test_source tests/node_links_ui_test.cpp)
        elseif(ui_test STREQUAL "NodeContent")
            set(ui_test_source tests/node_content_test.cpp)
        elseif(ui_test STREQUAL "MarkdownImport")
            set(ui_test_source tests/markdown_import_test.cpp)
        elseif(ui_test STREQUAL "DocumentFlow")
            set(ui_test_source tests/document_flow_test.cpp)
        elseif(ui_test STREQUAL "NodeOperations")
            set(ui_test_source tests/node_operations_test.cpp)
        elseif(ui_test STREQUAL "ShortcutsUi")
            set(ui_test_source tests/shortcuts_ui_test.cpp)
        elseif(ui_test STREQUAL "ImageStyle")
            set(ui_test_source tests/image_style_test.cpp)
        elseif(ui_test STREQUAL "CustomPalettes")
            set(ui_test_source tests/custom_palettes_test.cpp)
        elseif(ui_test STREQUAL "ThemeVariants")
            set(ui_test_source tests/theme_variants_test.cpp)
        elseif(ui_test STREQUAL "FileValidation")
            set(ui_test_source tests/file_validation_test.cpp)
        elseif(ui_test STREQUAL "TaskBox")
            set(ui_test_source tests/task_box_test.cpp)
        elseif(ui_test STREQUAL "Notes")
            set(ui_test_source tests/notes_test.cpp)
        elseif(ui_test STREQUAL "Labels")
            set(ui_test_source tests/labels_test.cpp)
        elseif(ui_test STREQUAL "MarkdownTyping")
            set(ui_test_source tests/markdown_typing_test.cpp)
        elseif(ui_test STREQUAL "NodeFiles")
            set(ui_test_source tests/node_files_test.cpp)
        elseif(ui_test STREQUAL "Formula")
            set(ui_test_source tests/formula_test.cpp)
        elseif(ui_test STREQUAL "StickersMarkers")
            set(ui_test_source tests/stickers_markers_test.cpp)
        elseif(ui_test STREQUAL "NodeSearch")
            set(ui_test_source tests/node_search_test.cpp)
        elseif(ui_test STREQUAL "SpellCheck")
            set(ui_test_source tests/spell_check_test.cpp)
        elseif(ui_test STREQUAL "WheelGuard")
            set(ui_test_source tests/wheel_guard_test.cpp)
        elseif(ui_test STREQUAL "Export")
            set(ui_test_source tests/export_test.cpp)  # FEAT-054 stage 2: PNG, SVG, PDF
        elseif(ui_test STREQUAL "Pitch")
            set(ui_test_source tests/pitch_test.cpp)  # FEAT-063 stage A
        elseif(ui_test STREQUAL "PitchTemplate")
            set(ui_test_source tests/pitch_template_test.cpp)  # FEAT-089
        elseif(ui_test STREQUAL "OfficeExport")
            set(ui_test_source tests/office_export_test.cpp)  # FEAT-054 stage 4: Word and Excel
        elseif(ui_test STREQUAL "FocusMode")
            set(ui_test_source tests/focus_mode_test.cpp)  # FEAT-087
        elseif(ui_test STREQUAL "Updates")
            set(ui_test_source tests/updates_test.cpp)  # FEAT-090
        elseif(ui_test STREQUAL "Help")
            set(ui_test_source tests/help_test.cpp)  # FEAT-092
        elseif(ui_test STREQUAL "Tour")
            set(ui_test_source tests/tour_test.cpp)  # FEAT-093
        else()
            set(ui_test_source tests/style_page_test.cpp)
        endif()
        qt_add_executable(Mentis${ui_test}Test ${ui_test_source})
        target_link_libraries(Mentis${ui_test}Test PRIVATE MentisCore MentisResources Qt6::Test)
        target_compile_definitions(Mentis${ui_test}Test PRIVATE MENTIS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME Mentis${ui_test} COMMAND Mentis${ui_test}Test)
        set_tests_properties(Mentis${ui_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        if(WIN32)
            set_tests_properties(Mentis${ui_test} PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endif()
    endforeach()

    # FEAT-019: cascata dello stile e operazioni su piu' nodi, senza widget.
    qt_add_executable(MentisNodeStyleTest tests/node_style_test.cpp)
    target_link_libraries(MentisNodeStyleTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisNodeStyle COMMAND MentisNodeStyleTest)
    set_tests_properties(MentisNodeStyle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisNodeStyle PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-053: relationships (data, geometry, document commands).
    qt_add_executable(MentisMapStyleResetTest tests/map_style_reset_test.cpp)
    target_link_libraries(MentisMapStyleResetTest PRIVATE MentisCore MentisResources Qt6::Test)
    add_test(NAME MentisMapStyleReset COMMAND MentisMapStyleResetTest)
    set_tests_properties(MentisMapStyleReset PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisMapStyleReset PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisGanttPlanTest tests/gantt_plan_test.cpp)
    target_link_libraries(MentisGanttPlanTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisGanttPlan COMMAND MentisGanttPlanTest)
    if(WIN32)
        set_tests_properties(MentisGanttPlan PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisGanttViewTest tests/gantt_view_test.cpp)
    target_link_libraries(MentisGanttViewTest PRIVATE MentisCore MentisResources Qt6::Test)
    add_test(NAME MentisGanttView COMMAND MentisGanttViewTest)
    set_tests_properties(MentisGanttView PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisGanttView PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisNumberingTest tests/numbering_test.cpp)
    target_link_libraries(MentisNumberingTest PRIVATE MentisCore MentisResources Qt6::Test)
    add_test(NAME MentisNumbering COMMAND MentisNumberingTest)
    set_tests_properties(MentisNumbering PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisNumbering PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisSelectionCommandsTest tests/selection_commands_test.cpp)
    target_link_libraries(MentisSelectionCommandsTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisSelectionCommands COMMAND MentisSelectionCommandsTest)
    set_tests_properties(MentisSelectionCommands PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisSelectionCommands PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisStackingOrderTest tests/stacking_order_test.cpp)
    target_link_libraries(MentisStackingOrderTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisStackingOrder COMMAND MentisStackingOrderTest)
    set_tests_properties(MentisStackingOrder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisStackingOrder PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    qt_add_executable(MentisRelationshipTest tests/relationship_test.cpp)
    target_link_libraries(MentisRelationshipTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisRelationship COMMAND MentisRelationshipTest)
    set_tests_properties(MentisRelationship PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisRelationship PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-055: conversione del Markdown (incolla nell'outliner e importazione da file), senza widget.
    # FEAT-070: colours of an SVG sticker found and replaced.
    qt_add_executable(MentisSvgDocumentTest tests/svg_document_test.cpp)
    target_link_libraries(MentisSvgDocumentTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisSvgDocument COMMAND MentisSvgDocumentTest)
    set_tests_properties(MentisSvgDocument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisSvgDocument PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    qt_add_executable(MentisMarkdownParseTest tests/markdown_parse_test.cpp)
    target_link_libraries(MentisMarkdownParseTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisMarkdownParse COMMAND MentisMarkdownParseTest)
    set_tests_properties(MentisMarkdownParse PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisMarkdownParse PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-068: scorciatoie Markdown mentre si scrive (motore, senza widget).
    qt_add_executable(MentisMarkdownShortcutsTest tests/markdown_shortcuts_test.cpp)
    target_link_libraries(MentisMarkdownShortcutsTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisMarkdownShortcuts COMMAND MentisMarkdownShortcutsTest)
    set_tests_properties(MentisMarkdownShortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisMarkdownShortcuts PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-066: il Document senza finestre (fogli, storico, file, salvataggio automatico).
    qt_add_executable(MentisDocumentTest tests/document_test.cpp)
    target_link_libraries(MentisDocumentTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisDocument COMMAND MentisDocumentTest)
    set_tests_properties(MentisDocument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisDocument PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-045: the MCP server of mentis-mcp, protocol (no windows) and tools on real files.
    # Phase 2: McpBridge, an agent on the map open in a real window.
    foreach(mcp_test IN ITEMS McpProtocol McpTools McpBridge)
        if(mcp_test STREQUAL "McpProtocol")
            qt_add_executable(MentisMcpProtocolTest tests/mcp_protocol_test.cpp)
            target_link_libraries(MentisMcpProtocolTest PRIVATE MentisCore Qt6::Test)
        elseif(mcp_test STREQUAL "McpTools")
            qt_add_executable(MentisMcpToolsTest tests/mcp_tools_test.cpp)
            target_link_libraries(MentisMcpToolsTest PRIVATE MentisCore MentisResources Qt6::Test)
        else()
            qt_add_executable(MentisMcpBridgeTest tests/mcp_bridge_test.cpp)
            target_link_libraries(MentisMcpBridgeTest PRIVATE MentisCore MentisResources Qt6::Test)
        endif()
        add_test(NAME Mentis${mcp_test} COMMAND Mentis${mcp_test}Test)
        set_tests_properties(Mentis${mcp_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        if(WIN32)
            set_tests_properties(Mentis${mcp_test} PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endif()
    endforeach()

    # FEAT-071: what Windows is told is under the mouse (edges, drag areas, Maximize button).
    qt_add_executable(MentisWindowFrameTest tests/windowframe_test.cpp)
    target_link_libraries(MentisWindowFrameTest PRIVATE MentisCore MentisResources Qt6::Test)
    add_test(NAME MentisWindowFrame COMMAND MentisWindowFrameTest)
    set_tests_properties(MentisWindowFrame PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisWindowFrame PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-035 parte A: token dell'interfaccia scura; controlla anche che i vecchi scuri non restino nei sorgenti.
    qt_add_executable(MentisUiThemeTest tests/uitheme_test.cpp)
    target_link_libraries(MentisUiThemeTest PRIVATE MentisCore MentisResources Qt6::Test)
    target_compile_definitions(MentisUiThemeTest PRIVATE MENTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME MentisUiTheme COMMAND MentisUiThemeTest)
    set_tests_properties(MentisUiTheme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisUiTheme PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-041: registro delle scorciatoie (predefinite, scelte, conflitti per area).
    qt_add_executable(MentisShortcutsTest tests/shortcuts_test.cpp)
    target_link_libraries(MentisShortcutsTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisShortcuts COMMAND MentisShortcutsTest)
    set_tests_properties(MentisShortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisShortcuts PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-046: lingua scelta, lingua effettiva e pagina Lingua delle Impostazioni.
    qt_add_executable(MentisSettingsUiTest tests/settings_ui_test.cpp)
    target_link_libraries(MentisSettingsUiTest PRIVATE MentisCore MentisResources Qt6::Test)
    add_test(NAME MentisSettingsUi COMMAND MentisSettingsUiTest)
    set_tests_properties(MentisSettingsUi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisSettingsUi PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-091: the JSON translation files (format, merge, 80% rule) and the files of translations/.
    qt_add_executable(MentisTranslationFilesTest tests/translation_files_test.cpp
        tools/translations/translationfiles.h tools/translations/translationfiles.cpp)
    target_include_directories(MentisTranslationFilesTest PRIVATE tools/translations)
    target_link_libraries(MentisTranslationFilesTest PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(MentisTranslationFilesTest PRIVATE
        MENTIS_TRANSLATIONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/translations")
    add_test(NAME MentisTranslationFiles COMMAND MentisTranslationFilesTest)
    if(WIN32)
        set_tests_properties(MentisTranslationFiles PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    qt_add_executable(MentisLocalizationTest tests/localization_test.cpp)
    target_link_libraries(MentisLocalizationTest PRIVATE MentisCore MentisResources Qt6::Test)
    # The compiled Italian translation (qt_add_lrelease writes it in the build folder).
    target_compile_definitions(MentisLocalizationTest PRIVATE MENTIS_QM_DIR="${CMAKE_BINARY_DIR}")
    add_test(NAME MentisLocalization COMMAND MentisLocalizationTest)
    set_tests_properties(MentisLocalization PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisLocalization PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # Fuzzing (2026-11-09): seeded mutations through the targets of tests/fuzz/fuzztargets.h.
    qt_add_executable(MentisFuzzTest tests/fuzz_test.cpp tests/fuzz/fuzztargets.cpp)
    target_include_directories(MentisFuzzTest PRIVATE tests)
    target_link_libraries(MentisFuzzTest PRIVATE MentisCore Qt6::Test)
    target_compile_definitions(MentisFuzzTest PRIVATE MENTIS_FUZZ_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus")
    add_test(NAME MentisFuzz COMMAND MentisFuzzTest)
    set_tests_properties(MentisFuzz PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisFuzz PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # Storico su mappe grandi: istantanea con cache per nodo e stati annullati/ripristinati.
    qt_add_executable(MentisHistorySnapshotTest tests/history_snapshot_test.cpp)
    target_link_libraries(MentisHistorySnapshotTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisHistorySnapshot COMMAND MentisHistorySnapshotTest)
    set_tests_properties(MentisHistorySnapshot PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisHistorySnapshot PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-027: collegamenti dei nodi senza widget (indirizzi, campi, passo di piu' fogli).
    qt_add_executable(MentisNodeLinksTest tests/node_links_test.cpp)
    target_link_libraries(MentisNodeLinksTest PRIVATE MentisCore Qt6::Test)
    add_test(NAME MentisNodeLinks COMMAND MentisNodeLinksTest)
    set_tests_properties(MentisNodeLinks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(MentisNodeLinks PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # FEAT-017: logica del contesto del pannello, senza widget.
    qt_add_executable(MentisPanelContextTest tests/panel_context_test.cpp src/panelcontext.h src/panelcontext.cpp)
    target_include_directories(MentisPanelContextTest PRIVATE src)
    target_link_libraries(MentisPanelContextTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME MentisPanelContext COMMAND MentisPanelContextTest)
    if(WIN32)
        set_tests_properties(MentisPanelContext PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    # Test areas (user, 2026-10-28; docs/engineering/quality-gates.md, "Test per area"): a change runs
    # the tests of the areas it touches plus "core", not the whole suite.
    #   cmake --build build --target tests-gantt tests-core   builds only those tests (and the app's library)
    #   ctest --test-dir build -L "gantt|core" -j 8           runs them
    # A test can be in more areas. A new test gets its areas here; CMake warns about one without.
    function(mentis_test_area area)
        if(NOT TARGET tests-${area})
            add_custom_target(tests-${area})
        endif()
        foreach(test IN LISTS ARGN)
            set_property(TEST Mentis${test} APPEND PROPERTY LABELS ${area})
            if(test STREQUAL "StorageRoundTrip")
                add_dependencies(tests-${area} MentisStorageTest)
            else()
                add_dependencies(tests-${area} Mentis${test}Test)
            endif()
        endforeach()
    endfunction()
    # Files, document, history and autosave: they guard the user's data, so they run with every change.
    mentis_test_area(core StorageRoundTrip Document DocumentFlow HistorySnapshot FileValidation Fuzz)
    mentis_test_area(map Tour MapLayout BalancedLayout NodeOperations NodeWidth NodeContent SelectionCommands StackingOrder
                     Relationship Numbering NodeSearch StickersMarkers TaskBox Labels Notes NodeFiles Formula NodeLinks
                     NodeLinksUi ImageStyle WheelGuard)
    mentis_test_area(outliner OutlinerSelection MarkdownTyping Notes TaskBox Numbering NodeSearch MarkdownImport)
    mentis_test_area(side-panel SidePanel PanelContext StylePage ColorPicker NodeStyle MapStyleReset CustomPalettes
                     ThemeVariants ImageStyle Numbering StickersMarkers WheelGuard SvgDocument)
    mentis_test_area(gantt GanttPlan GanttView)
    mentis_test_area(text MarkdownTyping MarkdownShortcuts SpellCheck Formula)
    mentis_test_area(import-export MarkdownImport MarkdownParse Export OfficeExport)
    mentis_test_area(settings ShortcutsUi Shortcuts SettingsUi Localization TranslationFiles UiTheme SpellCheck WindowFrame FocusMode Updates Help Tour)
    mentis_test_area(pitch Pitch PitchTemplate)
    mentis_test_area(mcp McpProtocol McpTools McpBridge)
    get_directory_property(mentis_tests TESTS)
    foreach(test IN LISTS mentis_tests)
        get_test_property(${test} LABELS test_labels)
        if(NOT test_labels)
            message(WARNING "Test ${test} has no area: add it to a mentis_test_area() call")
        endif()
    endforeach()
endif()
