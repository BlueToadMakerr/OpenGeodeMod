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
    CCNode* row = nullptr;
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
                {&update},
                nullptr
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




std::string getOriginalSourceID(std::string const& modID) {
    return readSetting("mod-source-index-" + modID, "");
}

std::string getOriginalSourceName(std::string const& modID) {
    auto source = getOriginalSourceID(modID);
    if (source.empty())
        return "Unknown";

    auto indexes = getAllIndexes();
    auto it = std::find_if(indexes.begin(), indexes.end(), [&](auto const& entry) {
        return entry.id == source;
    });
    return it != indexes.end() ? it->name : source;
}

std::string getOriginalSourceVersion(std::string const& modID) {
    return readSetting("mod-source-version-" + modID, "");
}

bool wasUpdatedFromSource(std::string const& modID, std::string const& indexID, std::string const& currentVersion) {
    return getOriginalSourceID(modID) == indexID &&
        getOriginalSourceVersion(modID) == currentVersion;
}

void addCheckmark(CCNode* parent, CCPoint position, float scale = 0.45f) {
    auto check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!check) return;
    check->setScale(scale);
    check->setPosition(position);
    parent->addChild(check, 5);
}

void addProgressBar(CCNode* row, float x, float y, float width, float progress) {
    auto background = CCLayerColor::create({50, 50, 50, 180}, width, 5.f);
    background->setPosition({x, y});
    row->addChild(background, 2);

    auto fill = CCLayerColor::create({153, 245, 245, 255}, width * std::clamp(progress, 0.f, 1.f), 5.f);
    fill->setPosition({0.f, 0.f});
    background->addChild(fill);
}

void updateProgressBar(CCNode* row, float x, float y, float width, float progress) {
    auto background = row->getChildByTag(9001);
    if (!background) return;
    auto fill = background->getChildByTag(9002);
    if (!fill) return;
    background->setPosition({x, y});
    fill->setContentSize({width * std::clamp(progress, 0.f, 1.f), 5.f});
}

void confirmIndexUpdate(
    IndexUpdateInfo update,
    CCNode* row = nullptr,
    std::function<void(bool)> finished = {}
) {
    auto source = getOriginalSourceID(update.modID);

    std::string warning;
    if (source.empty()) {
        warning =
            "\n\n<cr>The original index this mod was installed from is unknown.</c>"
            "\nIt will be updated from <cy>" + update.indexName + "</c>.";
    } else if (source != update.indexID) {
        warning = fmt::format(
            "\n\n<cr>Originally downloaded from <cy>{}</c>.</c>"
            "\nIt will be updated from <cy>{}</c> instead.",
            getOriginalSourceName(update.modID),
            update.indexName
        );
    }

    createQuickPopup(
        "Update Mod",
        fmt::format(
            "Update <cy>{}</c> from <cg>{}</c>?\n"
            "Version: <cy>{}</c> -> <cg>{}</c>{}",
            update.modName,
            update.indexName,
            update.currentVersion,
            update.newVersion,
            warning
        ),
        "Cancel",
        "Update",
        [update = std::move(update), row, finished = std::move(finished)](auto, bool confirmed) mutable {
            if (!confirmed) {
                if (finished) finished(false);
                return;
            }

            auto progressBar = CCNode::create();
            progressBar->setTag(9001);
            auto background = CCLayerColor::create({50, 50, 50, 180}, 130.f, 5.f);
            background->setTag(9001);
            auto fill = CCLayerColor::create({153, 245, 245, 255}, 0.f, 5.f);
            fill->setTag(9002);
            background->addChild(fill);
            progressBar->addChild(background);
            if (row) {
                row->addChild(progressBar, 5);
                progressBar->setPosition({42.f, row->getContentSize().height - 75.f});
                row->retain();
            }

            downloadIndexUpdate(
                update,
                [row, progressBar, finished = std::move(finished), update](bool success) mutable {
                    if (success && row && row->getParent()) {
                        if (auto old = row->getChildByID("restart-badge"))
                            old->removeFromParent();

                        auto restart = makeStatusTag(
                            "Restart Required",
                            {153, 245, 245}
                        );
                        restart->setID("restart-badge");
                        restart->setAnchorPoint({0.f, 0.5f});
                        restart->setPosition({40.f, row->getContentSize().height - 60.f});
                        row->addChild(restart, 6);

                        if (auto button = row->getChildByID("update-button"))
                            button->removeFromParent();

                        addCheckmark(row, {row->getContentSize().width - 50.f, row->getContentSize().height - 28.f});
                        auto sourceLabel = CCLabelBMFont::create(
                            update.indexName.c_str(),
                            "bigFont.fnt"
                        );
                        sourceLabel->setScale(0.24f);
                        sourceLabel->setAnchorPoint({1.f, 0.5f});
                        sourceLabel->setPosition({
                            row->getContentSize().width - 58.f,
                            row->getContentSize().height - 28.f
                        });
                        row->addChild(sourceLabel, 5);
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

                    if (progressBar)
                        progressBar->removeFromParent();

                    if (row) row->release();
                    if (finished) finished(success);
                },
                [row, progressBar](float progress) {
                    if (!row || !progressBar || !row->getParent())
                        return;

                    auto background = progressBar->getChildByTag(9001);
                    auto fill = background ? background->getChildByTag(9002) : nullptr;
                    if (!fill) return;

                    fill->setContentSize({
                        130.f * std::clamp(progress, 0.f, 1.f),
                        5.f
                    });
                }
            );
        },
        true
    );
}

class UpdateSourcePopup : public Popup {
    std::vector<IndexUpdateInfo> m_options;
    std::function<void(IndexUpdateInfo)> m_callback;

    bool init() {
        if (!Popup::init(330.f, 260.f, getPopupBackground()))
            return false;

        setTitle("Choose Update Source");

        if (auto close = createGeodeCloseButton())
            setCloseButtonSpr(close, 0.875f);

        auto size = m_mainLayer->getScaledContentSize();
        auto originalID = getOriginalSourceID(m_options.front().modID);
        auto originalVersion = getOriginalSourceVersion(m_options.front().modID);

        auto original = CCLabelBMFont::create(
            fmt::format(
                "Originally downloaded from: {}{}",
                getOriginalSourceName(m_options.front().modID),
                originalVersion.empty() ? "" : fmt::format(" ({})", originalVersion)
            ).c_str(),
            "bigFont.fnt"
        );
        original->setScale(0.27f);
        original->setAnchorPoint({0.5f, 0.5f});
        original->setPosition({size.width / 2.f, size.height - 38.f});
        m_mainLayer->addChild(original);

        auto menu = CCMenu::create();
        menu->setContentSize({size.width - 30.f, size.height - 85.f});
        menu->setPosition({15.f, 15.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f));

        for (auto const& option : m_options) {
            auto button = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create(
                    fmt::format("{}  {} -> {}", option.indexName, option.currentVersion, option.newVersion).c_str(),
                    "goldFont.fnt",
                    getButtonTexture("GE_button_01.png"),
                    0.48f
                ),
                [this, option](auto) {
                    auto callback = std::move(m_callback);
                    this->onClose(nullptr);
                    if (callback)
                        callback(option);
                }
            );

            if (option.indexID == originalID) {
                addCheckmark(button, {
                    button->getContentSize().width - 12.f,
                    button->getContentSize().height / 2.f
                }, 0.35f);

                if (!originalVersion.empty() && originalVersion == option.currentVersion) {
                    auto current = CCLabelBMFont::create("Installed", "bigFont.fnt");
                    current->setScale(0.18f);
                    current->setAnchorPoint({1.f, 0.5f});
                    current->setPosition({
                        button->getContentSize().width - 18.f,
                        button->getContentSize().height / 2.f - 10.f
                    });
                    button->addChild(current, 6);
                }
            }

            menu->addChild(button);
        }

        menu->updateLayout();
        m_mainLayer->addChild(menu);
        return true;
    }

public:
    static UpdateSourcePopup* create(
        std::vector<IndexUpdateInfo> options,
        std::function<void(IndexUpdateInfo)> callback
    ) {
        auto ret = new UpdateSourcePopup();
        ret->m_options = std::move(options);
        ret->m_callback = std::move(callback);
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void chooseUpdateSource(
    GroupedUpdate const& group,
    CCNode* row = nullptr,
    std::function<void(bool)> finished = {}
) {
    if (group.indexes.size() <= 1) {
        if (!group.indexes.empty())
            confirmIndexUpdate(*group.indexes.front(), row, std::move(finished));
        else if (finished)
            finished(false);
        return;
    }

    std::vector<IndexUpdateInfo> options;
    for (auto const* update : group.indexes)
        options.push_back(*update);

    UpdateSourcePopup::create(
        std::move(options),
        [row, finished = std::move(finished)](IndexUpdateInfo selected) mutable {
            confirmIndexUpdate(std::move(selected), row, std::move(finished));
        }
    )->show();
}

class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(380.f, 320.f, getPopupBackground()))
            return false;

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

        auto scrollSize = CCSize{size.width - 20.f, size.height - 118.f};

        auto scrollBackground = NineSlice::create("square02_001.png");
        scrollBackground->setContentSize(scrollSize);
        scrollBackground->setAnchorPoint({0.5f, 0.5f});
        scrollBackground->setPosition({
            size.width / 2.f,
            58.f + scrollSize.height / 2.f
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
            58.f
        });
        m_mainLayer->addChild(scroll);

        auto groups = groupUpdates();
        float totalHeight = 0.f;

        for (auto const& group : groups)
            totalHeight += 92.f + static_cast<float>(group.indexes.size()) * 24.f;

        totalHeight = std::max(totalHeight, scroll->getContentSize().height);

        float y = totalHeight;

        for (auto& group : groups) {
            float rowHeight = 92.f + static_cast<float>(group.indexes.size()) * 24.f;
            y -= rowHeight;

            auto row = CCMenu::create();
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

            group.row = row;

            {
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
                        [group, row](auto) {
                            chooseUpdateSource(group, row);
                        }
                    );
                    updateButton->setID("update-button");
                    updateButton->setScale(0.55f);
                    updateButton->m_baseScale = 0.55f;
                    updateButton->setPosition({
                        scroll->getContentSize().width - 50.f,
                        rowHeight - 28.f
                    });
                    row->addChild(updateButton, 10);
                    handleTouchPriority(updateButton, true);
                }
            }

            float progressY = rowHeight - 82.f;
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
                version->setPosition({8.f, progressY});
                row->addChild(version);
                progressY -= 24.f;
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

        auto updateAllButton = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create(
                "Update All",
                "goldFont.fnt",
                getButtonTexture("GE_button_01.png"),
                0.55f
            ),
            [groups](auto) {
                auto sharedGroups = std::make_shared<std::vector<GroupedUpdate>>(groups);
                auto runNext = std::make_shared<std::function<void(size_t)>>();
                *runNext = [sharedGroups, runNext](size_t index) {
                    if (index >= sharedGroups->size())
                        return;

                    chooseUpdateSource(
                        (*sharedGroups)[index],
                        (*sharedGroups)[index].row,
                        [runNext, index](bool) {
                            (*runNext)(index + 1);
                        }
                    );
                };
                (*runNext)(0);
            }
        );

        auto bottomMenu = CCMenu::create();
        bottomMenu->setPosition({size.width / 2.f, 18.f});
        bottomMenu->addChild(updateAllButton);
        bottomMenu->updateLayout();
        m_mainLayer->addChild(bottomMenu);

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
    fetchIndexUpdates([] {
        inferOriginalIndexSources([] {
            UpdatesPopup::create()->show();
        });
    }, true);
}

} // namespace opengeode
