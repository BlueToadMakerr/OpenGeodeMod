#pragma once

#include "Settings.hpp"

#include <functional>

namespace opengeode {
void showModifyIndexPopup(IndexEntry entry, std::function<void()> onSaved);
}
