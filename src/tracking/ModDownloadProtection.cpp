#include "ModDownloadProtection.hpp"
#include "../InstalledMods.hpp"
#include "../PopupSectionUtils.hpp"
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/ui/Popup.hpp>
using namespace geode::prelude;
namespace opengeode {
    namespace {
        std::string getModIDFromDownloadURL(std::string const & url) {
            constexpr std::string_view prefix = "https://api.geode-sdk.org/v1/mods/";
            if (!url.starts_with(prefix)) return "";
            auto start = prefix.size();
            auto end = url.find('/', start);
            if (end == std::string::npos || end <= start)
                return "";
            auto endpoint = url.substr(end);
            bool isDownload = endpoint.starts_with("/download") || (endpoint.starts_with("/versions/") && endpoint.find("/download") != std::string::npos);
            if (!isDownload)
                return "";
            return url.substr(start, end - start);
        }
        std::string getCurrentIndexName() {
            auto current = getIndexUrl();
            for (auto const & entry: getAllIndexes()) {
                if (entry.url == current)
                    return entry.name;
            }
            return current;
        }
        class AlreadyUpdatedPopup: public Popup {
            bool init(std::string modName, std::string installedIndexName, std::string currentIndexName, std::string through) {
                if (!Popup::init(360.f, 220.f, getPopupBackground())) return false;
                setTitle("Already Updated!");
                if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close,.875f);
                auto text = fmt::format("You already <cg>updated</c> <cy>{}</c> through <cj>{}</c> from <cy>{}</c>.\nn\nn" "You are trying to install from <cy>{}</c>. To change the updated through this index (or any other index), click the <cj>Open Geode</c> button and <cy>redownload</c> the update.",
                modName, through, installedIndexName, currentIndexName);
                auto area = MDTextArea::create(text, {
                    325.f, 145.f
                }
                , true);
                if (!area)
                    return false;
                area->setAnchorPoint( {
                    .5f,.5f
                }
                );
                area->setPosition( {
                    m_mainLayer->getContentWidth() / 2.f, 108.f
                }
                );
                area->getScrollLayer()->m_cutContent = false;
                area->getScrollLayer()->m_disableMovement = false;
                area->getScrollLayer()->setMouseEnabled(true);
                m_mainLayer->addChild(area);
                return true;
            }
            public: static AlreadyUpdatedPopup * create(std::string modName, std::string installedIndexName, std::string currentIndexName,
            std::string through) {
                auto ret = new AlreadyUpdatedPopup();
                if (ret && ret->init(std::move(modName), std::move(installedIndexName), std::move(currentIndexName), std::move(through))) {
                    ret->autorelease();
                    return ret;
                }
                delete ret;
                return nullptr;
            }
        };
    }
    bool isGeodeModDownloadRequest(std::string const & url, std::string & modID) {
        modID = getModIDFromDownloadURL(url);
        return !modID.empty();
    }
    void showAlreadyUpdatedPopup(std::string const & modID) {
        auto mod = Loader::get()->getInstalledMod(modID);
        std::string modName = mod ? std::string(mod->getName()): modID;
        auto source = getInstalledModSource(modID);
        auto installedIndexName = source ?(source->indexName.empty() ? source->indexId: source->indexName): std::string("Unknown index");
        auto currentIndexName = getCurrentIndexName();
        auto through = wasModUpdatedFromGeode(modID) ? std::string("Geode"): std::string("Open Geode");
        Loader::get()->queueInMainThread([modName = std::move(modName), installedIndexName = std::move(installedIndexName),
        currentIndexName = std::move(currentIndexName), through = std::move(through)] {
            if (auto popup = AlreadyUpdatedPopup::create(std::move(modName), std::move(installedIndexName), std::move(currentIndexName),
            std::move(through))) popup->show();
        }
        );
    }
    bool blockAlreadyUpdatedModDownload(utils::web::WebRequest & request, std::string const & modID) {
        if (modID.empty() || !wasModUpdatedFromIndex(modID)) return false;
        request.url("https://opengeode.invalid/already-updated/" + modID);
        showAlreadyUpdatedPopup(modID);
        log::info("Blocked regular Geode download for {} because it was already updated through OpenGeode", modID);
        return true;
    }
}
// namespace opengeode
