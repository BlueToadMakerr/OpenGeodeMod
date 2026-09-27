#include "IndexUpdates.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

struct GroupedUpdate {
    std::string modID;
    std::string modName;
    std::string currentVersion;
    bool disabled = false;
    bool outdated = false;
    std::vector<IndexUpdateInfo const*> indexes;
};

std::vector<GroupedUpdate> groupUpdates() {
    std::vector<GroupedUpdate> result;
    for (auto const& update : indexUpdates()) {
        auto it = std::find_if(result.begin(), result.end(), [&](auto const& group) {
            return group.modID == update.modID;
        });
        if (it == result.end()) {
            result.push_back({update.modID, update.modName, update.currentVersion,
                              update.disabled, update.outdated, {&update}});
        } else {
            it->indexes.push_back(&update);
            it->disabled |= update.disabled;
            it->outdated |= update.outdated;
        }
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
        return a.modName < b.modName;
    });
    return result;
}

ccColor3B statusColor(GroupedUpdate const& update) {
    if (update.outdated)
        return ColorProvider::get()->color3b("geode.loader/mod-list-outdated-label");
    if (update.disabled)
        return {255, 65, 65};
    return {255, 255, 255};
}

std::string statusText(GroupedUpdate const& update) {
    if (update.outdated && update.disabled) return "Disabled • Outdated";
    if (update.outdated) return "Outdated";
    if (update.disabled) return "Disabled";
    return "Update Available";
}

CCNode* makeStatusTag(GroupedUpdate const& update) {
    auto text = CCLabelBMFont::create(statusText(update).c_str(), "bigFont.fnt");
    text->setScale(0.24f);
    text->setColor(statusColor(update));
    text->setAnchorPoint({0.5f, 0.5f});

    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({text->getScaledContentWidth() + 10.f, 16.f});
    tag->setScaleMultiplier(0.5f);
    tag->setOpacity(90);
    tag->setColor(statusColor(update));
    tag->addChild(text);
    text->setPosition(tag->getScaledContentSize() / 2.f);
    return tag;
}

class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(380.f, 300.f, getPopupBackground())) return false;
        setTitle("Available Updates");
        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, 0.875f);

        auto scroll = ScrollLayer::create({340.f, 235.f});
        scroll->setPosition({20.f, 42.f});
        m_mainLayer->addChild(scroll);

        auto groups = groupUpdates();
        float totalHeight = 0.f;
        for (auto const& group : groups)
            totalHeight += 70.f + static_cast<float>(group.indexes.size()) * 16.f;
        totalHeight = std::max(totalHeight, scroll->getContentSize().height);

        float y = totalHeight;
        for (auto const& group : groups) {
            float rowHeight = 70.f + static_cast<float>(group.indexes.size()) * 16.f;
            y -= rowHeight;
            auto row = CCNode::create();
            row->setContentSize({340.f, rowHeight});
            row->setPosition({0.f, y});

            auto mod = Loader::get()->getLoadedMod(group.modID);
            if (mod) {
                auto icon = createModLogo(mod);
                if (icon) {
                    icon->setScale(0.42f);
                    icon->setPosition({20.f, rowHeight - 22.f});
                    row->addChild(icon);
                }
            }

            auto name = CCLabelBMFont::create(group.modName.c_str(), "bigFont.fnt");
            name->setScale(0.38f);
            name->setAnchorPoint({0.f, 0.5f});
            name->setPosition({40.f, rowHeight - 16.f});
            row->addChild(name);

            auto id = CCLabelBMFont::create(group.modID.c_str(), "bigFont.fnt");
            id->setScale(0.23f);
            id->setOpacity(125);
            id->setAnchorPoint({0.f, 0.5f});
            id->setPosition({40.f, rowHeight - 31.f});
            row->addChild(id);

            auto tag = makeStatusTag(group);
            tag->setAnchorPoint({0.f, 0.5f});
            tag->setPosition({40.f, rowHeight - 47.f});
            row->addChild(tag);

            float versionY = rowHeight - 66.f;
            for (auto const* update : group.indexes) {
                auto version = CCLabelBMFont::create(
                    fmt::format("{}: {} -> {}", update->indexName, update->currentVersion, update->newVersion).c_str(),
                    "bigFont.fnt"
                );
                version->setScale(0.24f);
                version->setAnchorPoint({0.f, 0.5f});
                version->setPosition({40.f, versionY});
                row->addChild(version);
                versionY -= 16.f;
            }
            scroll->m_contentLayer->addChild(row);
        }

        if (groups.empty()) {
            auto label = CCLabelBMFont::create("No updates available", "bigFont.fnt");
            label->setScale(0.45f);
            label->setPosition({170.f, 117.5f});
            scroll->m_contentLayer->addChild(label);
        }
        scroll->m_contentLayer->setContentSize({340.f, totalHeight});
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
