#include "ModDownloadProtection.hpp"
#include "../InstalledMods.hpp"
#include "../PopupSectionUtils.hpp"

#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

namespace opengeode {
namespace {

std::string getModIDFromDownloadURL(std::string const& url) {
    constexpr std::string_view prefix = "https://api.geode-sdk.org/v1/mods/";
    if (!url.starts_with(prefix)) return "";

    auto start = prefix.size();
    auto end = url.find('/', start);
    if (end == std::string::npos || end <= start) return "";

    auto endpoint = url.substr(end);
    bool isDownload = endpoint.starts_with("/download") ||
        (endpoint.starts_with("/versions/") && endpoint.find("/download") != std::string::npos);
    if (!isDownload) return "";

    return url.substr(start, end - start);
}

class AlreadyUpdatedPopup : public Popup {
    bool init(std::string modName) {
        if (!Popup::init(360.f, 205.f, getPopupBackground())) return false;
        setTitle("Already Updated!");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

        auto text = fmt::format(
            "You already updated <cy>{}</c> through Open Geode.\n\n"
            "To change where the update is installed from, click the Open Geode button and redownload the update from your preferred source.",
            modName
        );
        auto area = MDTextArea::create(text, {325.f, 130.f}, true);
        if (!area) return false;
        area->setAnchorPoint({.5f, .5f});
        area->setPosition({m_mainLayer->getContentWidth() / 2.f, 101.f});
        area->getScrollLayer()->m_cutContent = false;
        area->getScrollLayer()->m_disableMovement = false;
        area->getScrollLayer()->setMouseEnabled(true);
        m_mainLayer->addChild(area);
        return true;
    }

public:
    static AlreadyUpdatedPopup* create(std::string modName) {
        auto ret = new AlreadyUpdatedPopup();
        if (ret && ret->init(std::move(modName))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

}

bool isGeodeModDownloadRequest(std::string const& url, std::string& modID) {
    modID = getModIDFromDownloadURL(url);
    return !modID.empty();
}

void showAlreadyUpdatedPopup(std::string const& modID) {
    auto mod = Loader::get()->getInstalledMod(modID);
    std::string modName = mod ? std::string(mod->getName()) : modID;
    Loader::get()->queueInMainThread([modName = std::move(modName)] {
        if (auto popup = AlreadyUpdatedPopup::create(modName)) popup->show();
    });
}

bool blockAlreadyUpdatedModDownload(geode::web::WebRequest& request, std::string const& modID) {
    if (modID.empty() || !wasModUpdatedFromIndex(modID)) return false;

    request.url("https://opengeode.invalid/already-updated/" + modID);
    showAlreadyUpdatedPopup(modID);
    log::info("Blocked regular Geode download for {} because it was already updated through OpenGeode", modID);
    return true;
}

} // namespace opengeode
