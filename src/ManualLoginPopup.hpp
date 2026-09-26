#pragma once

#include "Settings.hpp"

namespace opengeode {
void showManualLoginPopup(IndexEntry entry, std::function<void()> onSaved = {});
} // namespace opengeode
