#pragma once
// The worker's windows on Linux: zenity (which the Steam Linux Runtime ships, and GNOME
// and most desktops have), else kdialog. They run as separate programs, so Lua can no
// more drive them than it can drive the worker; text from addons is shown as plain text.
#ifndef _WIN32
#include <optional>
#include <string>
#include <vector>
namespace mmd::dialogs {
struct Filter {std::string label;std::vector<std::string> patterns;};  // patterns: "*.pmx"
// Paths the player chose; nullopt when cancelled. Throws std::runtime_error when no dialog
// program is available. An answer within the first moment (a click or Enter meant for the
// game) does not count: the window opens again.
std::optional<std::vector<std::string>> pickFiles(const std::string& title,const std::vector<Filter>& filters,bool multiple,bool folder);
// A question with up to three buttons; buttons[defaultButton] answers Enter and closing the
// window (pass the safe choice). Returns the index pressed. Choices other than the default
// count only after the window has been open for a moment.
int choose(const std::string& title,const std::string& text,const std::vector<std::string>& buttons,int defaultButton);
// A message with one button.
void inform(const std::string& title,const std::string& text);
}
#endif
