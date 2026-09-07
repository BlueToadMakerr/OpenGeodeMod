#pragma once

#include <Geode/Geode.hpp>

#include <functional>

namespace opengeode {

void showAccountPopup();
void showGdLoginPopup(std::function<void()> onLoggedIn = {});

} // namespace opengeode
