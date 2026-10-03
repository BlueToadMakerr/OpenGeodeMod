#pragma once
#include <Geode/Geode.hpp>
#include <functional>
namespace opengeode {
    void showAccountPopup();
    void showGithubLoginPopup(std::function < void() > onLoggedIn = {
    }
    );
}
// namespace opengeode
