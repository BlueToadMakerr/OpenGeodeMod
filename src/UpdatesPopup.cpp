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
            result.push_back({
                update.modID,
                update.modName,
                update.currentVersion,
                update.disabled,
                update.outdated,
                {&update}
            });
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

CCNode* makeStatusTag(std::string const& text, ccColor3B color) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(0.22f);
    label->setColor({255, 255, 255});
    label->setAnchorPoint({0.5f, 0.5f});

    auto tag = NineSlice::create("square02_001.png");
    tag->setContentSize({
        label->getScaledContentWidth() + 12.f,
        15.f
    });
    tag->setOpacity(130);
    tag->setColor(color);
    tag->addChild(label);
    label->setPosition(tag->getScaledContentSize() / 2.f);
    return tag;
}

void addStatusTags(CCNode* row, GroupedUpdate const& update, float x, float y) {
    float tagX = x;

    if (update.disabled) {
        auto tag = makeStatusTag("Disabled", {255, 90, 90});
        tag->setAnchorPoint({0.f, 0.5f});
        tag->setPosition({tagX, y});
        row->addChild(tag);
        tagX += tag->getScaledContentWidth() + 4.f;
    }

    if (update.outdated) {
        auto tag = makeStatusTag("Outdated", {255, 190, 70});
        tag->setAnchorPoint({0.f, 0.5f});
        tag->setPosition({tagX, y});
        row->addChild(tag);
        tagX += tag->getScaledContentWidth() + 4.f;
    }
}

class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(380.f, 300.f, getPopupBackground())) return false;
        setTitle("Available Updates");

        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, 0.875f);

        auto size = m_mainLayer->getScaledContentSize();

        auto totalLabel = CCLabelBMFont::create(
            fmt::format("{} updates available", getTotalUpdateCount()).c_str(),
            "goldFont.fnt"
        );
        totalLabel->setScale(0.38f);
        totalLabel->setAnchorPoint({0.5f, 0.5f});
        totalLabel->setPosition({size.width / 2.f, size.height - 34.f});
        m_mainLayer->addChild(totalLabel);

        auto scrollSize = CCSize{size.width - 20.f, size.height - 82.f};

        auto scrollBackground = NineSlice::create("square02_001.png");
        scrollBackground->setContentSize(scrollSize);
        scrollBackground->setAnchorPoint({0.5f, 0.5f});
        scrollBackground->setPosition({
            size.width / 2.f,
            40.f + scrollSize.height / 2.f
        });
        scrollBackground->setOpacity(50);
        m_mainLayer->addChild(scrollBackground);

        auto scroll = ScrollLayer::create({
            scrollSize.width - 10.f,
            scrollSize.height - 10.f
        });
        scroll->setAnchorPoint({0.5f, 0.5f});
        scroll->setPosition({
            size.width / 2.f - scroll->getContentSize().width / 2.f,
            40.f
        });
        m_mainLayer->addChild(scroll);

        auto groups = groupUpdates();
        float totalHeight = 0.f;

        for (auto const& group : groups)
            totalHeight += 70.f + static_cast<float>(group.indexes.size()) * 27.f;

        totalHeight = std::max(totalHeight, scroll->getContentSize().height);

        float y = totalHeight;

        for (auto const& group : groups) {
            float rowHeight = 70.f + static_cast<float>(group.indexes.size()) * 27.f;
            y -= rowHeight;

            auto row = CCNode::create();
            row->setContentSize({scroll->getContentSize().width, rowHeight});
            row->setPosition({0.f, y});

            if (auto mod = Loader::get()->getLoadedMod(group.modID)) {
                if (auto icon = createModLogo(mod)) {
                    icon->setScale(0.42f);
                    icon->setPosition({20.f, rowHeight - 22.f});
                    row->addChild(icon);
                }
            }

            auto name = CCLabelBMFont::create(group.modName.c_str(), "bigFont.fnt");
            name->setScale(0.38f);
            name->setAnchorPoint({0.f, 0.5f});
            name->setPosition({40.f, rowHeight - 15.f});
            row->addChild(name);

            auto id = CCLabelBMFont::create(group.modID.c_str(), "bigFont.fnt");
            id->setScale(0.22f);
            id->setOpacity(125);
            id->setAnchorPoint({0.f, 0.5f});
            id->setPosition({40.f, rowHeight - 30.f});
            row->addChild(id);

            addStatusTags(row, group, 40.f, rowHeight - 45.f);

            float versionY = rowHeight - 66.f;

            for (auto const* update : group.indexes) {
                auto version = CCLabelBMFont::create(
                    fmt::format(
                        "{}: {} -> {}",
                        update->indexName,
                        update->currentVersion,
                        update->newVersion
                    ).c_str(),
                    "bigFont.fnt"
                );
                version->setScale(0.24f);
                version->setAnchorPoint({0.f, 0.5f});
                version->setPosition({8.f, versionY});
                row->addChild(version);

                auto updateSprite = CCSprite::createWithSpriteFrameName(
                    "geode.loader/update.png"
                );
                auto circle = CircleButtonSprite::create(
                    updateSprite,
                    isGeodeTheme() ? CircleBaseColor::DarkPurple : CircleBaseColor::Green,
                    CircleBaseSize::Small
                );

                if (circle) {
                    auto updateButton = CCMenuItemExt::createSpriteExtra(
                        circle,
                        [updateCopy = *update, row](auto) {
                            auto source = readSetting(
                                "mod-source-index-" + updateCopy.modID,
                                ""
                            );

                            std::string warning;
                            if (source.empty()) {
                                warning =
                                    "\n\n<cr>The original index this mod was installed from is unknown.</c>"
                                    "\nThis update will be installed from <cy>" + updateCopy.indexName + "</c>.";
                            } else if (source != updateCopy.indexID) {
                                auto indexes = getAllIndexes();
                                auto sourceIt = std::find_if(
                                    indexes.begin(),
                                    indexes.end(),
                                    [&](auto const& entry) {
                                        return entry.id == source;
                                    }
                                );
                                auto sourceName = sourceIt != indexes.end()
                                    ? sourceIt->name
                                    : source;

                                warning = fmt::format(
                                    "\n\n<cr>This mod was originally installed from {}.</c>"
                                    "\nIt is being updated from <cy>{}</c> instead.",
                                    sourceName,
                                    updateCopy.indexName
                                );
                            }

                            createQuickPopup(
                                "Update Mod",
                                fmt::format(
                                    "Update <cy>{}</c> from <cg>{}</c>?\n"
                                    "Version: <cy>{}</c> -> <cg>{}</c>{}",
                                    updateCopy.modName,
                                    updateCopy.indexName,
                                    updateCopy.currentVersion,
                                    updateCopy.newVersion,
                                    warning
                                ),
                                "Cancel",
                                "Update",
                                [updateCopy, row](auto, bool confirmed) {
                                    if (!confirmed) return;

                                    row->retain();
                                    downloadIndexUpdate(
                                        updateCopy,
                                        [row](bool success) {
                                            if (success && row->getParent()) {
                                                auto restart = makeStatusTag(
                                                    "Restart Required",
                                                    {153, 245, 245}
                                                );
                                                restart->setAnchorPoint({0.f, 0.5f});
                                                restart->setPosition({150.f, 25.f});
                                                row->addChild(restart);
                                            }

                                            if (success) {
                                                Notification::create(
                                                    "Update downloaded — restart required",
                                                    NotificationIcon::Success,
                                                    3.f
                                                )->show();
                                            } else {
                                                Notification::create(
                                                    "Failed to download update",
                                                    NotificationIcon::Error,
                                                    3.f
                                                )->show();
                                            }

                                            row->release();
                                        }
                                    );
                                },
                                true
                            );
                        }
                    );

                    updateButton->setScale(0.55f);
                    updateButton->setPosition({
                        scroll->getContentSize().width - 17.f,
                        versionY
                    });
                    row->addChild(updateButton);
                }

                versionY -= 27.f;
            }

            scroll->m_contentLayer->addChild(row);
        }

        if (groups.empty()) {
            auto label = CCLabelBMFont::create(
                "No updates available",
                "bigFont.fnt"
            );
            label->setScale(0.45f);
            label->setPosition({
                scroll->getContentSize().width / 2.f,
                scroll->getContentSize().height / 2.f
            });
            scroll->m_contentLayer->addChild(label);
        }

        scroll->m_contentLayer->setContentSize({
            scroll->getContentSize().width,
            totalHeight
        });
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

} // namespace

void showUpdatesPopup() {
    UpdatesPopup::create()->show();
}

} // namespace opengeode
