#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMenuItem.hpp>

using namespace geode::prelude;

namespace opengeode {

class $modify(TabTrackerMenuItem, CCMenuItem) {
    void activate() {
        auto id = this->getID();

        // Update the cached tab before Geode handles the click. The native
        // activation can synchronously trigger a reload, so doing this after
        // CCMenuItem::activate() lets that request briefly use the previous
        // tab's filter parameters.
        if (id == "installed-button" ||
            id == "download-button" ||
            id == "recent-button" ||
            id == "featured-button") {
            getCachedActiveTabKey() = id;
        }

        CCMenuItem::activate();
    }
};

} // namespace opengeode
