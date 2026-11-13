#pragma once

// FEAT-097: what the mobile app sets up after QGuiApplication and before its first page, shared
// with the tests: the interface font, the translators and the Qt Quick Controls style.
namespace MobileApp {
// Before QGuiApplication: the windows' multisampling.
void prepareSurfaceFormat();
void prepare();
}
