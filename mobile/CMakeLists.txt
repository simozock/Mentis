# FEAT-097 / ADR-006: Mentis for Android, a Qt Quick front end on MentisDomain. No Qt Widgets here:
# MentisDomain and MentisFormula (FEAT-116) are the only Mentis libraries linked. Built when MENTIS_BUILD_MOBILE is on (default on
# Android only); on the desktop it runs as a window, to try and test the pages without a phone.
# Qt Quick and Qt Quick Controls are found by the top CMakeLists.txt.
# The QML modules' resources under :/qt/qml, which the engine imports from without an import path
# (https://doc.qt.io/qt-6/qt-cmake-policy-qtp0001.html); a qmldir for the qml/ folder of the
# module (https://doc.qt.io/qt-6/qt-cmake-policy-qtp0004.html).
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# The adapters and pages, a static QML module shared by the app and its tests. Static, so the app
# links the module's plugin and imports it with Q_IMPORT_QML_PLUGIN (qt_add_qml_module, "static
# backing target", https://doc.qt.io/qt-6/qt-add-qml-module.html).
qt_add_library(MentisMobileUi STATIC
    mapsnapshot.h
    mapsnapshot.cpp
    maplibrary.h
    maplibrary.cpp
    mobilemap.h
    mobilemap.cpp
    mobilestyle.h
    mobilestyle.cpp
    mobilesheetstyle.cpp
    mobilepitch.h
    mobilepitch.cpp
    mobileoutline.h
    mobileoutline.cpp
    mobilegantt.h
    mobilegantt.cpp
    mobilegantttask.h
    mobilegantttask.cpp
    mobileresources.h
    mobileresources.cpp
    mobileganttstyle.h
    mobileganttstyle.cpp
    outlineimage.h
    outlineimage.cpp
    mapexport.h
    mapexport.cpp
    mobileshare.h
    variantsample.h
    variantsample.cpp
    mapscene.h
    mapscene.cpp
    mapcanvas.h
    mapcanvas.cpp
    mobileapp.h
    mobileapp.cpp
    penasmouse.h
    penasmouse.cpp
    hardwarekeyboard.h
    hardwarekeyboard.cpp
    controlkey.h
    controlkey.cpp
    stickerlibrary.h
    stickerlibrary.cpp
    mobilestickereditor.h
    mobilestickereditor.cpp
    stickercanvasview.h
    stickercanvasview.cpp
    imagepicker.h
    imagepicker.cpp
    markericon.h
    markericon.cpp
    formulapreview.h
    formulapreview.cpp
    formulafield.h
    formulafield.cpp
    latexlist.h
    latexlist.cpp
    markdowntyping.h
    markdowntyping.cpp
    bracetyping.h
    bracetyping.cpp
    keycommands.h
    keycommands.cpp
    incomingmaps.h
    incomingmaps.cpp
    storageaccess.h
    storageaccess.cpp
    panelsize.h
    panelsize.cpp
)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(MentisMobileUi
    URI Mentis.Mobile
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Mentis/Mobile"
    QML_FILES
        qml/Main.qml
        qml/HomePage.qml
        qml/MapPage.qml
        qml/Theme.qml
        qml/FloatingGroup.qml
        qml/GroupButton.qml
        qml/NameTip.qml
        qml/TipLabel.qml
        qml/TipArea.qml
        qml/SheetSelect.qml
        qml/ModalDialog.qml
        qml/DarkButton.qml
        qml/StickerPicker.qml
        qml/StickerEditorDialog.qml
        qml/NoteDialog.qml
        qml/LinkDialog.qml
        qml/LabelsDialog.qml
        qml/FilesMenu.qml
        qml/DarkSwitch.qml
        qml/LabelMenu.qml
        qml/MarkerMenu.qml
        qml/SheetMenu.qml
        qml/SearchPanel.qml
        qml/FolderIcon.qml
        qml/FormulaDialog.qml
        qml/LatexListDialog.qml
        qml/AddMenu.qml
        qml/DarkSegments.qml
        qml/SidePanel.qml
        qml/StylePanelPage.qml
        qml/MapPanelPage.qml
        qml/PitchPanelPage.qml
        qml/PitchThemeSheet.qml
        qml/PitchTemplateFields.qml
        qml/MapExportSheet.qml
        qml/ExportOptionsSheet.qml
        qml/PresentationView.qml
        qml/NumberingFields.qml
        qml/PaletteSheet.qml
        qml/MapTypeSheet.qml
        qml/NewPaletteSheet.qml
        qml/StyleSection.qml
        qml/StyleSelectRow.qml
        qml/StyleChoiceMenu.qml
        qml/StyleStepper.qml
        qml/ColorSheet.qml
        qml/OutlinerView.qml
        qml/BaselinesDialog.qml
        qml/ColumnsDialog.qml
        qml/GanttChartView.qml
        qml/GanttTaskPage.qml
        qml/ResourcesDialog.qml
        qml/GanttStylePage.qml
        qml/GanttColorRow.qml
        qml/DatePickerSheet.qml
        qml/SettingsPage.qml
)
target_include_directories(MentisMobileUi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# FEAT-116: MentisFormula draws the formulas (JKQTMathText, a shared library as on the desktop).
target_link_libraries(MentisMobileUi PUBLIC MentisDomain MentisFormula Qt6::Quick Qt6::QuickControls2)
mentis_warnings(MentisMobileUi)
# PenAsMouse hands a pen's events to QGuiApplication as a platform's mouse
# (QWindowSystemInterface, Qt's QPA API, private: https://doc.qt.io/qt-6/qpa.html).
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
target_link_libraries(MentisMobileUi PRIVATE Qt6::GuiPrivate)
if(ANDROID)
    # ImagePicker starts the system picker for a result (QtAndroidPrivate::startActivity, private
    # Qt Core API since Qt 6.2). From Qt 6.9 private modules are packages of their own.
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
        find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    endif()
    target_link_libraries(MentisMobileUi PRIVATE Qt6::CorePrivate)
endif()

qt_add_executable(MentisMobile main.cpp)
# MentisResources: the fonts of the map (NodeGeometry::loadBundledFonts) and of the interface.
target_link_libraries(MentisMobile PRIVATE MentisMobileUi MentisMobileUiplugin MentisResources)
mentis_warnings(MentisMobile)
if(ANDROID)
    # The desktop's icon (assets/appicon) on the launcher: mobile/android/res holds it at Android's
    # densities, plus an adaptive version (Android 8 and later).
    # FEAT-105 stage 4: the manifest is Qt's own template of the Qt in use (its placeholders and
    # attributes change between versions: 6.8 and 6.12 differ) with Mentis's intent filters after
    # the launcher's, written with res/ into a package folder in the build tree.
    # https://doc.qt.io/qt-6/android-manifest-file-configuration.html
    set(mentis_android_package "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    set(mentis_qt_manifest "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_DATA}/src/android/templates/AndroidManifest.xml")
    set(mentis_intent_filters "${CMAKE_CURRENT_SOURCE_DIR}/android/intent-filters.xml")
    if(NOT EXISTS "${mentis_qt_manifest}")
        message(FATAL_ERROR "Qt's Android manifest template not found: ${mentis_qt_manifest}")
    endif()
    file(READ "${mentis_qt_manifest}" mentis_manifest)
    file(READ "${mentis_intent_filters}" mentis_filters)
    string(FIND "${mentis_manifest}" "</intent-filter>" mentis_filter_end)
    if(mentis_filter_end EQUAL -1)
        message(FATAL_ERROR "No launcher intent filter in ${mentis_qt_manifest}")
    endif()
    string(REPLACE "</intent-filter>" "</intent-filter>\n${mentis_filters}" mentis_manifest "${mentis_manifest}")
    # FEAT-105 decision 1: the Mentis folder at the root of the shared storage. Android 11 and later:
    # the access to all files; Android 9 and 10: the storage permission, and on 10 the legacy
    # storage (ignored from 11). https://developer.android.com/training/data-storage/manage-all-files
    # https://developer.android.com/training/data-storage/use-cases#opt-out-in-production-app
    string(FIND "${mentis_manifest}" "<!-- %%INSERT_PERMISSIONS -->" mentis_permissions_at)
    if(mentis_permissions_at EQUAL -1)
        message(FATAL_ERROR "No permissions placeholder in ${mentis_qt_manifest}")
    endif()
    string(REPLACE "<!-- %%INSERT_PERMISSIONS -->"
        "<uses-permission android:name=\"android.permission.MANAGE_EXTERNAL_STORAGE\" />\n    <uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\" android:maxSdkVersion=\"29\" />\n    <!-- %%INSERT_PERMISSIONS -->"
        mentis_manifest "${mentis_manifest}")
    string(FIND "${mentis_manifest}" "requestLegacyExternalStorage" mentis_legacy_at)
    if(mentis_legacy_at EQUAL -1)
        string(REPLACE "android:hardwareAccelerated=\"true\"" "android:hardwareAccelerated=\"true\"\n        android:requestLegacyExternalStorage=\"true\"" mentis_manifest "${mentis_manifest}")
    endif()
    # The name under the launcher icon: Qt writes the target's (MentisMobile) here, and
    # QT_ANDROID_APP_NAME exists only from Qt 6.9.
    string(REPLACE "-- %%INSERT_APP_NAME%% --" "Mentis" mentis_manifest "${mentis_manifest}")
    # The launcher icon, written here too: QT_ANDROID_APP_ICON (below) exists only from Qt 6.9, and
    # Qt 6.8's template (the CI's) has no icon attribute, so its APK showed Android's default icon.
    string(REPLACE "-- %%INSERT_APP_ICON%% --" "@mipmap/ic_launcher" mentis_manifest "${mentis_manifest}")
    string(FIND "${mentis_manifest}" "android:icon=" mentis_icon_at)
    if(mentis_icon_at EQUAL -1)
        string(FIND "${mentis_manifest}" "android:hardwareAccelerated=\"true\"" mentis_accel_at)
        if(mentis_accel_at EQUAL -1)
            message(FATAL_ERROR "No place for the launcher icon in ${mentis_qt_manifest}")
        endif()
        string(REPLACE "android:hardwareAccelerated=\"true\"" "android:hardwareAccelerated=\"true\"\n        android:icon=\"@mipmap/ic_launcher\"" mentis_manifest "${mentis_manifest}")
    endif()
    file(WRITE "${mentis_android_package}/AndroidManifest.xml.new" "${mentis_manifest}")
    file(COPY_FILE "${mentis_android_package}/AndroidManifest.xml.new" "${mentis_android_package}/AndroidManifest.xml" ONLY_IF_DIFFERENT)
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/android/res" DESTINATION "${mentis_android_package}")
    file(GLOB_RECURSE mentis_android_res CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/android/res/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mentis_qt_manifest}" "${mentis_intent_filters}" ${mentis_android_res})
    qt_policy(SET QTP0002 NEW)  # the paths below are plain strings
    # Release 1.1.0 (first APK on GitHub): the package name is the app's identity on the phone and
    # in the stores, never changed after a release; the version comes from project(Mentis VERSION),
    # the code grows with it (1.1.0 -> 10100) so a new APK installs over the old one.
    math(EXPR mentis_android_version_code
        "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    set_target_properties(MentisMobile PROPERTIES
        QT_ANDROID_PACKAGE_NAME "io.github.simozock.mentis"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${mentis_android_version_code}"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${mentis_android_package}"
        QT_ANDROID_APP_ICON "@mipmap/ic_launcher")
endif()
