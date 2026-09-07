#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMenuItem.hpp>

using namespace geode::prelude;

namespace opengeode {

class $modify(TabTrackerMenuItem, CCMenuItem) {
    void activate() {
        auto id = this->getID();

        CCMenuItem::activate();

        if (id == "installed-button" ||
            id == "download-button" ||
            id == "recent-button" ||
            id == "featured-button") {
            getCachedActiveTabKey() = id;
        }
    }
};

} // namespace opengeode
