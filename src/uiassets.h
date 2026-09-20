#pragma once

#include <string>

// The single-page app, embedded in the binary. Generated at configure time
// from src/ui/* by cmake/uiassets.cpp.in — there is no asset directory to
// install, no path to get wrong, and no build step in between.
namespace Ui {

std::string indexHtml();
std::string styleCss();
std::string appJs();

} // namespace Ui
