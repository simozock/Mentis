# JKQTMathText (renderer LaTeX per formule, parte di JKQtPlotter di Jan W. Krieger)
# Fonte: https://github.com/jkriege2/JKQtPlotter  - commit in UPSTREAM_COMMIT
# Licenza: LGPL 2.1 o successiva (LICENSE-LGPL-2.1.txt). Font Fira Math: SIL OFL 1.1.
#
# Viene compilata come libreria CONDIVISA (DLL/.so/.dylib) separata dall'app: con la
# LGPL questo è il modo più semplice di rispettare il diritto dell'utente di
# sostituire la libreria, anche se Mentis diventasse un prodotto commerciale.
# Si compilano solo le parti necessarie (niente plotter, niente font XITS da 11 MB).

file(GLOB BC_JKQTCOMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jkqtcommon/*.cpp)
# Strumenti immagine non usati dal renderer di formule.
list(FILTER BC_JKQTCOMMON_SOURCES EXCLUDE REGEX "jkqtpbasicimagetools\\.cpp$")
file(GLOB BC_JKQTMATHTEXT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/jkqtmathtext/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jkqtmathtext/nodes/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jkqtmathtext/parsers/*.cpp)
file(GLOB_RECURSE BC_JKQTMATHTEXT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
# FEAT-116: built without Qt Widgets, also for Android. Left out: the widget geometry helpers, the
# icons and the QLabel subclass, which use widgets and serve neither the renderer nor Mentis.
list(FILTER BC_JKQTCOMMON_SOURCES EXCLUDE REGEX "(jkqttools|jkqtpicons)\\.cpp$")
list(FILTER BC_JKQTMATHTEXT_SOURCES EXCLUDE REGEX "jkqtmathtextlabel\\.cpp$")
list(FILTER BC_JKQTMATHTEXT_HEADERS EXCLUDE REGEX "(jkqtmathtextlabel|jkqttools|jkqtpicons|jkqtpbasicimagetools|_precomp)\\.h$")

add_library(MentisMathText SHARED
    ${BC_JKQTCOMMON_SOURCES}
    ${BC_JKQTMATHTEXT_SOURCES}
    ${BC_JKQTMATHTEXT_HEADERS}
    jkqtmathtext/resources/firamath.qrc
)
set_target_properties(MentisMathText PROPERTIES AUTOMOC ON AUTORCC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(MentisMathText PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MentisMathText
    PUBLIC  JKQTCOMMON_LIB_IN_DLL JKQTMATHTEXT_LIB_IN_DLL JKQTMATHTEXT_COMPILED_WITH_FIRAMATH
    PRIVATE JKQTCOMMON_LIB_EXPORT_LIBRARY JKQTMATHTEXT_LIB_EXPORT_LIBRARY)
target_link_libraries(MentisMathText PUBLIC Qt6::Core Qt6::Gui)
