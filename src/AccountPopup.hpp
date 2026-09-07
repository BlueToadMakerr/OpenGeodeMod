#pragma once

#include <Geode/Geode.hpp>

namespace opengeode {

void showAccountPopup();
void showGdLoginPopup(std::function<void()> onLoggedIn = {});

} // namespace opengeode
