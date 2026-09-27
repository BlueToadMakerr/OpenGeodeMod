#include "IndexUpdates.hpp"
#include "updates/UpdateModItem.hpp"
#include "PopupSectionUtils.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
using namespace geode::prelude;
namespace opengeode {
namespace {
struct UpdateGroup { std::string modID; std::vector<IndexUpdateInfo> sources; };
std::vector<UpdateGroup> groupUpdates() {
    std::vector<UpdateGroup> groups;
    for (auto const& update : indexUpdates()) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](auto const& group) { return group.modID == update.modID; });
        if (it == groups.end()) groups.push_back({update.modID, {update}});
        else it->sources.push_back(update);
    }
    std::sort(groups.begin(), groups.end(), [](auto const& a, auto const& b) {
        return (a.sources.empty() ? a.modID : a.sources.front().modName) < (b.sources.empty() ? b.modID : b.sources.front().modName);
    });
    return groups;
}
IndexUpdateInfo highestUpdate(std::vector<IndexUpdateInfo> const& sources) {
    return *std::max_element(sources.begin(), sources.end(), [](auto const& a, auto const& b) { return parseUpdateVersion(a.newVersion) < parseUpdateVersion(b.newVersion); });
}
class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(390.f, 295.f, getPopupBackground())) return false;
        setTitle("Updates");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);
        auto size = m_mainLayer->getScaledContentSize();
        auto groups = groupUpdates();
        auto total = CCLabelBMFont::create(fmt::format("{} update{} available", groups.size(), groups.size() == 1 ? "" : "s").c_str(), "goldFont.fnt");
        total->setScale(.31f);
        total->setPosition({size.width / 2.f, size.height - 31.f});
        m_mainLayer->addChild(total);
        auto listBG = NineSlice::create(getSectionBackground());
        listBG->setContentSize({size.width - 20.f, size.height - 72.f});
        listBG->setOpacity(70);
        listBG->setPosition({size.width / 2.f, size.height / 2.f - 7.f});
        m_mainLayer->addChild(listBG);
        auto scroll = ScrollLayer::create({size.width - 30.f, size.height - 84.f});
        scroll->setPosition({15.f, 31.f});
        scroll->m_contentLayer->setContentWidth(scroll->getContentWidth());
        constexpr float cardHeight = 72.f;
        constexpr float gap = 5.f;
        auto contentHeight = std::max(scroll->getContentHeight(), static_cast<float>(groups.size()) * (cardHeight + gap) + gap);
        scroll->m_contentLayer->setContentSize({scroll->getContentWidth(), contentHeight});
        float y = contentHeight - gap - cardHeight / 2.f;
        for (auto const& group : groups) {
            auto item = UpdateModItem::create(highestUpdate(group.sources), group.sources);
            if (!item) continue;
            item->setPosition({scroll->getContentWidth() / 2.f, y});
            scroll->m_contentLayer->addChild(item);
            y -= cardHeight + gap;
        }
        m_mainLayer->addChild(scroll);
        return true;
    }
public:
    static UpdatesPopup* create() {
        auto ret = new UpdatesPopup();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};
}
void showUpdatesPopup() {
    fetchIndexUpdates([] { inferOriginalIndexSources([] { UpdatesPopup::create()->show(); }); }, false);
}
}
