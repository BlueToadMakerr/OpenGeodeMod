#include "IndexUpdates.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

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

        auto const& updates = getIndexUpdates();
        float rowHeight = 46.f;
        float totalHeight = std::max(rowHeight * updates.size(), scroll->getContentSize().height);
        float y = totalHeight;

        for (auto const& update : updates) {
            y -= rowHeight;

            auto row = CCNode::create();
            row->setContentSize({320.f, rowHeight});
            row->setPosition({0.f, y});

            auto modLabel = CCLabelBMFont::create(update.modName.c_str(), "bigFont.fnt");
            modLabel->setScale(0.42f);
            modLabel->setAnchorPoint({0.f, 0.5f});
            modLabel->setPosition({5.f, 29.f});
            row->addChild(modLabel);

            auto status = CCLabelBMFont::create(
                update.disabled ? "Disabled • Outdated" : "Outdated",
                "chatFont.fnt"
            );
            status->setScale(0.5f);
            status->setColor(update.disabled ? ccYELLOW : ccGRAY);
            status->setAnchorPoint({0.f, 0.5f});
            status->setPosition({5.f, 13.f});
            row->addChild(status);

            auto version = CCLabelBMFont::create(
                fmt::format("{} -> {}", update.currentVersion, update.newVersion).c_str(),
                "chatFont.fnt"
            );
            version->setScale(0.5f);
            version->setAnchorPoint({1.f, 0.5f});
            version->setPosition({315.f, 29.f});
            row->addChild(version);

            auto indexLabel = CCLabelBMFont::create(
                update.indexName.c_str(),
                "chatFont.fnt"
            );
            indexLabel->setScale(0.5f);
            indexLabel->setAnchorPoint({1.f, 0.5f});
            indexLabel->setPosition({315.f, 13.f});
            row->addChild(indexLabel);

            scroll->m_contentLayer->addChild(row);
        }

        if (updates.empty()) {
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
