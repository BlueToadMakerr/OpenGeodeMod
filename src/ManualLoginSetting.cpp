#include "ManualLoginPopup.hpp"
#include "Settings.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>

using namespace geode::prelude;

namespace opengeode {

// "manual-login" is a built-in `button` setting (see mod.json); this only handles its press.
$execute {
    ButtonSettingPressedEventV3(Mod::get(), "manual-login").listen([](std::string_view buttonKey) {
        if (buttonKey != "configure") return;
        auto url = getIndexUrl();
        IndexEntry entry;
        for (auto const& candidate : getAllIndexes()) if (candidate.url == url) { entry = candidate; break; }
        if (entry.url.empty()) { entry.id = getActiveIndexId(); entry.name = "Active Index"; entry.url = url; }
        showManualLoginPopup(std::move(entry));
    }).leak();
}

} // namespace opengeode
