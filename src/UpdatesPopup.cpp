#include "IndexUpdates.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <map>

using namespace geode::prelude;

namespace opengeode {

class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(360.f, 300.f, getPopupBackground())) return false;
        setTitle("Available Updates");

        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, 0.875f);

        auto scroll = ScrollLayer::create({320.f, 235.f});
        scroll->setPosition({20.f, 42.f});
        m_mainLayer->addChild(scroll);

        // Group all index results by mod. A mod can therefore have several
        // update rows when multiple indexes provide a newer version.
        std::map<std::string, std::vector<IndexUpdateInfo>> grouped;
        for (auto const& update : indexUpdates())
            grouped[update.modID].push_back(update);

        constexpr float rowHeight = 50.f;
        float totalHeight = std::max(rowHeight * static_cast<float>(grouped.size()), scroll->getContentSize().height);
        float y = totalHeight;

        for (auto const& [modID, modUpdates] : grouped) {
            y -= rowHeight;

            auto row = CCNode::create();
            row->setContentSize({320.f, rowHeight});
            row->setPosition({0.f, y});

            auto const& first = modUpdates.front();
            auto modLabel = CCLabelBMFont::create(first.modName.c_str(), "bigFont.fnt");
            modLabel->setScale(0.42f);
            modLabel->setAnchorPoint({0.f, 0.5f});
            modLabel->setPosition({5.f, 37.f});
            row->addChild(modLabel);

            // Match Geode/FavoriteMods' status semantics: a mod may be both
            // disabled and outdated, so don't turn every update into an
            // "Outdated" status just because an update exists.
            std::string statusText;
            bool disabled = false;
            bool outdated = false;
            for (auto const& update : modUpdates) {
                disabled |= update.disabled;
                outdated |= update.outdated;
            }

            if (disabled && outdated) statusText = "Disabled • Outdated";
            else if (disabled) statusText = "Disabled";
            else if (outdated) statusText = "Outdated";

            if (!statusText.empty()) {
                auto status = CCLabelBMFont::create(statusText.c_str(), "chatFont.fnt");
                status->setScale(0.5f);
                status->setColor(disabled ? ccYELLOW : ColorProvider::get()->color3b("geode.loader/mod-list-outdated-label"));
                status->setAnchorPoint({0.f, 0.5f});
                status->setPosition({5.f, 20.f});
                row->addChild(status);
            }

            // Each index gets its own line, while the mod name is shown only once.
            float indexY = 37.f;
            for (auto const& update : modUpdates) {
                auto indexLine = CCLabelBMFont::create(
                    fmt::format("{}: {} -> {}", update.indexName, update.currentVersion, update.newVersion).c_str(),
                    "chatFont.fnt"
                );
                indexLine->setScale(0.45f);
                indexLine->setAnchorPoint({1.f, 0.5f});
                indexLine->setPosition({315.f, indexY});
                row->addChild(indexLine);
                indexY -= 14.f;
            }

            scroll->m_contentLayer->addChild(row);
        }

        if (grouped.empty()) {
            auto label = CCLabelBMFont::create("No updates available", "bigFont.fnt");
            label->setScale(0.45f);
            label->setPosition({160.f, 117.5f});
            scroll->m_contentLayer->addChild(label);
        }

        scroll->m_contentLayer->setContentSize({320.f, totalHeight});
        scroll->scrollToTop();
        return true;
    }

public:
    static UpdatesPopup* create() {
        auto ret = new UpdatesPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void showUpdatesPopup() {
    UpdatesPopup::create()->show();
}

} // namespace opengeode
