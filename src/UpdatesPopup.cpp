#include "IndexUpdates.hpp"
#include "updates/UpdateModItem.hpp"
#include "updates/UpdateSourcePopup.hpp"
#include "PopupSectionUtils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/SimpleAxisLayout.hpp>

using namespace geode::prelude;

namespace opengeode {

namespace {

struct UpdateGroup {
    std::string modID;
    std::vector<IndexUpdateInfo> sources;
};

std::vector<UpdateGroup> groupUpdates() {
    std::vector<UpdateGroup> groups;
    for (auto const& update : indexUpdates()) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](auto const& group) {
            return group.modID == update.modID;
        });
        if (it == groups.end()) groups.push_back({update.modID, {update}});
        else it->sources.push_back(update);
    }

    std::sort(groups.begin(), groups.end(), [](auto const& a, auto const& b) {
        return (a.sources.empty() ? a.modID : a.sources.front().modName) <
            (b.sources.empty() ? b.modID : b.sources.front().modName);
    });
    return groups;
}

IndexUpdateInfo highestUpdate(std::vector<IndexUpdateInfo> const& sources) {
    return *std::max_element(
        sources.begin(),
        sources.end(),
        [](auto const& a, auto const& b) {
            return parseUpdateVersion(a.newVersion) < parseUpdateVersion(b.newVersion);
        }
    );
}

class BatchUpdateState : public std::enable_shared_from_this<BatchUpdateState> {
public:
    std::vector<UpdateGroup> groups;
    size_t current = 0;
    bool alwaysInstalled = false;
    int pending = 0;
    int successful = 0;
    int failed = 0;
    std::unordered_map<std::string, UpdateModItem*> items;
    CCMenuItemSpriteExtra* updateAllButton = nullptr;
    std::function<void()> finished;

    ~BatchUpdateState() {
        for (auto const& [_, item] : items) {
            if (item) item->release();
        }
        if (updateAllButton) updateAllButton->release();
    }

    void next() {
        if (current >= groups.size()) {
            if (pending == 0) {
                auto finishedCallback = std::move(finished);
                if (finishedCallback) finishedCallback();
            }
            return;
        }

        auto group = groups[current++];
        if (group.sources.empty()) {
            next();
            return;
        }

        auto installed = getInstalledModSource(group.modID);
        auto installedIt = installed
            ? std::find_if(group.sources.begin(), group.sources.end(), [&](auto const& source) {
                return source.indexID == installed->indexId;
            })
            : group.sources.end();

        if (group.sources.size() == 1) {
            download(group.sources.front());
            next();
            return;
        }

        if (alwaysInstalled && installedIt != group.sources.end()) {
            download(*installedIt);
            next();
            return;
        }

        if (installedIt != group.sources.end()) {
            auto installedSource = *installedIt;
            auto weak = weak_from_this();
            UpdateSourcePopup::create(
                group.sources,
                [weak](IndexUpdateInfo selected) {
                    if (auto state = weak.lock()) {
                        state->download(selected);
                        state->next();
                    }
                },
                [weak] {
                    if (auto state = weak.lock()) state->next();
                },
                [weak, installedSource] {
                    if (auto state = weak.lock()) {
                        state->alwaysInstalled = true;
                        state->download(installedSource);
                        state->next();
                    }
                }
            )->show();
            return;
        }

        auto weak = weak_from_this();
        createQuickPopup(
            "Update Conflict",
            fmt::format(
                "{} has updates from multiple indexes, but its installed "
                "source could not be identified. Choose an update source to continue.",
                group.sources.front().modName
            ),
            "Skip",
            "Choose Source",
            [weak, sources = group.sources](auto, bool chooseSource) {
                auto state = weak.lock();
                if (!state) return;
                if (!chooseSource) {
                    state->next();
                    return;
                }
                UpdateSourcePopup::create(
                    sources,
                    [weak](IndexUpdateInfo selected) {
                        if (auto state = weak.lock()) {
                            state->download(selected);
                            state->next();
                        }
                    },
                    [weak] {
                        if (auto state = weak.lock()) state->next();
                    }
                )->show();
            },
            true
        );
    }

    void download(IndexUpdateInfo update) {
        ++pending;
        auto weak = weak_from_this();
        auto it = items.find(update.modID);
        if (it != items.end() && it->second) {
            it->second->updateWithoutConfirmation(
                std::move(update),
                [weak](bool success) {
                    if (auto state = weak.lock()) {
                        if (success) ++state->successful;
                        else ++state->failed;
                        --state->pending;
                        if (state->current >= state->groups.size() && state->pending == 0)
                            state->next();
                    }
                }
            );
            return;
        }

        downloadIndexUpdate(
            std::move(update),
            [weak](bool success) {
                if (auto state = weak.lock()) {
                    if (success) ++state->successful;
                    else ++state->failed;
                    --state->pending;
                    if (state->current >= state->groups.size() && state->pending == 0)
                        state->next();
                }
            }
        );
    }
};

std::shared_ptr<BatchUpdateState>& activeBatch() {
    static std::shared_ptr<BatchUpdateState> batch;
    return batch;
}

void startUpdateAll(
    std::vector<UpdateGroup> groups,
    std::unordered_map<std::string, UpdateModItem*> items,
    CCMenuItemSpriteExtra* updateAllButton
) {
    if (groups.empty() || activeBatch()) return;

    auto state = std::make_shared<BatchUpdateState>();
    state->groups = std::move(groups);
    state->updateAllButton = updateAllButton;
    if (state->updateAllButton) state->updateAllButton->retain();
    for (auto const& [modID, item] : items) {
        if (item) {
            item->retain();
            state->items.emplace(modID, item);
        }
    }
    state->finished = [weak = std::weak_ptr<BatchUpdateState>(state)] {
        auto state = weak.lock();
        if (!state) return;

        if (state->successful == static_cast<int>(state->groups.size()) && state->updateAllButton) {
            state->updateAllButton->setVisible(false);
            state->updateAllButton->setEnabled(false);
        }

        if (state->successful > 0) {
            Notification::create(
                fmt::format(
                    "Updated {} mod{} — restart required",
                    state->successful,
                    state->successful == 1 ? "" : "s"
                ).c_str(),
                NotificationIcon::Success,
                3.f
            )->show();
        }

        if (state->failed > 0) {
            Notification::create(
                fmt::format(
                    "{} update{} failed",
                    state->failed,
                    state->failed == 1 ? "" : "s"
                ).c_str(),
                NotificationIcon::Error,
                3.f
            )->show();
        }

        activeBatch().reset();
    };
    activeBatch() = state;
    state->next();
}

class UpdatesPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(390.f, 285.f, getPopupBackground())) return false;
        setTitle("Updates");
        if (auto close = createGeodeCloseButton()) setCloseButtonSpr(close, .875f);

        auto size = m_mainLayer->getScaledContentSize();
        auto groups = groupUpdates();

        auto total = CCLabelBMFont::create(
            fmt::format(
                "{} update{} available",
                groups.size(),
                groups.size() == 1 ? "" : "s"
            ).c_str(),
            "goldFont.fnt"
        );
        total->setScale(.30f);
        total->setPosition({size.width / 2.f, size.height - 30.f});
        m_mainLayer->addChild(total);

        auto listBG = NineSlice::create(getSectionBackground());
        listBG->setContentSize({size.width - 20.f, 184.f});
        listBG->setOpacity(90);
        listBG->setColor({0, 0, 0});
        listBG->setPosition({size.width / 2.f, 139.f});
        m_mainLayer->addChild(listBG);

        auto scroll = ScrollLayer::create({size.width - 30.f, 174.f});
        scroll->setPosition({15.f, 52.f});
        scroll->m_contentLayer->setContentWidth(scroll->getContentWidth());

        constexpr float cardHeight = 58.f;
        constexpr float gap = 2.f;
        auto contentHeight = std::max(
            scroll->getContentHeight(),
            static_cast<float>(groups.size()) * (cardHeight + gap) + gap
        );
        scroll->m_contentLayer->setContentSize({
            scroll->getContentWidth(),
            contentHeight
        });

        float y = contentHeight - gap - cardHeight / 2.f;
        std::unordered_map<std::string, UpdateModItem*> items;
        for (auto const& group : groups) {
            auto item = UpdateModItem::create(highestUpdate(group.sources), group.sources);
            if (!item) continue;
            item->setPosition({scroll->getContentWidth() / 2.f, y});
            scroll->m_contentLayer->addChild(item);
            items[group.modID] = item;
            y -= cardHeight + gap;
        }

        if (groups.empty()) {
            auto label = CCLabelBMFont::create("No updates available", "bigFont.fnt");
            label->setScale(.40f);
            label->setPosition({
                scroll->getContentWidth() / 2.f,
                scroll->getContentHeight() / 2.f
            });
            scroll->m_contentLayer->addChild(label);
        }

        m_mainLayer->addChild(scroll);

        auto buttons = CCMenu::create();
        buttons->setAnchorPoint({0.f, 0.f});
        buttons->setContentSize({size.width - 20.f, 36.f});
        buttons->setPosition({10.f, 6.f});

        auto updateAllSprite = ButtonSprite::create(
            "Update All",
            "bigFont.fnt",
            getButtonTexture("GJ_button_01.png"),
            .42f
        );
        CCMenuItemSpriteExtra* updateAll = nullptr;
        updateAll = CCMenuItemExt::createSpriteExtra(
            updateAllSprite,
            [groups, items, &updateAll](CCMenuItemSpriteExtra*) {
                if (groups.empty() || activeBatch()) return;

                createQuickPopup(
                    "Update All",
                    fmt::format(
                        "Are you sure you want to update {} mods?",
                        groups.size()
                    ),
                    "Cancel",
                    "Update All",
                    [groups, items](auto, bool confirmed) {
                        if (confirmed) startUpdateAll(groups, items, updateAll);
                    },
                    true
                );
            }
        );
        updateAll->setID("update-all-button");
        updateAll->setEnabled(!activeBatch());
        updateAll->setPosition({buttons->getContentWidth() / 2.f - updateAll->getScaledContentWidth() / 2.f - 3.f, 18.f});
        buttons->addChild(updateAll);

        auto restartSprite = ButtonSprite::create(
            "Restart",
            "bigFont.fnt",
            getButtonTexture("GJ_button_01.png"),
            .42f
        );
        auto restart = CCMenuItemExt::createSpriteExtra(
            restartSprite,
            [](CCMenuItemSpriteExtra*) {
                game::restart(true);
            }
        );
        restart->setID("restart-button");
        bool restartRequired = !completedIndexUpdates().empty();
        if (!restartRequired) {
            restartRequired = std::any_of(groups.begin(), groups.end(), [](auto const& group) {
                return wasModUpdatedFromIndex(group.modID);
            });
        }
        restart->setVisible(restartRequired);
        restart->setEnabled(restartRequired);
        restart->setPosition({buttons->getContentWidth() / 2.f + restart->getScaledContentWidth() / 2.f + 3.f, 18.f});
        buttons->addChild(restart);

        m_mainLayer->addChild(buttons, 10);
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

}

void showUpdatesPopup() {
    fetchIndexUpdates([] {
        inferOriginalIndexSources([] {
            UpdatesPopup::create()->show();
        });
    }, false);
}

} // namespace opengeode
