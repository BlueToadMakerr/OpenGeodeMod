#include "IndexListPopup.hpp"
#include "AddIndexPopup.hpp"
#include "ModifyIndexPopup.hpp"
#include "PopupSectionUtils.hpp"
#include "PresetIndexPopup.hpp"
#include "Settings.hpp"
#include "IndexUpdates.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

namespace opengeode {

void showUpdatesPopup();

class IndexListPopup : public Popup {
protected:
    ScrollLayer* m_scrollLayer = nullptr;

    bool init() {
        if (!Popup::init(340.f, 280.f, getPopupBackground())) return false;
        this->setTitle("Index Selector");
        if (auto close = createGeodeCloseButton())
            this->setCloseButtonSpr(close, 0.875f);

        float centerX = m_mainLayer->getContentWidth() / 2;

        m_scrollLayer = ScrollLayer::create({300.f, 190.f});
        m_scrollLayer->setPosition({centerX - 150.f, 60.f});
        m_mainLayer->addChild(m_scrollLayer);

        rebuildList();
        fetchIndexUpdates([this] { rebuildList(); });

        auto addBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("+ Add", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), 0.6f),
            [this](auto) {
                showAddIndexPopup([this] {
                    invalidateIndexUpdateCache();
                    rebuildList();
                    fetchIndexUpdates([this] { rebuildList(); });
                });
            }
        );

        auto presetBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Presets", "goldFont.fnt", getButtonTexture("GJ_button_02.png"), 0.6f),
            [this](auto) {
                showPresetIndexPopup([this] {
                    invalidateIndexUpdateCache();
                    rebuildList();
                    fetchIndexUpdates([this] { rebuildList(); });
                });
            }
        );

        auto updatesBtn = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Updates", "goldFont.fnt", getButtonTexture("GJ_button_03.png"), 0.6f),
            [](auto) { showUpdatesPopup(); }
        );

        auto bottomMenu = CCMenu::create();
        bottomMenu->addChild(addBtn);
        bottomMenu->addChild(presetBtn);
        bottomMenu->addChild(updatesBtn);
        bottomMenu->setLayout(RowLayout::create()->setGap(7.f));
        bottomMenu->setPosition({centerX, 25.f});
        bottomMenu->updateLayout();
        m_mainLayer->addChild(bottomMenu);

        return true;
    }

    void rebuildList() {
        if (!m_scrollLayer) return;
        m_scrollLayer->m_contentLayer->removeAllChildren();

        auto entries = getAllIndexes();
        float contentWidth = m_scrollLayer->getContentSize().width;
        float rowHeight = 34.f;
        float totalHeight = std::max(entries.size() * rowHeight, m_scrollLayer->getContentSize().height);

        float y = totalHeight;
        for (auto const& entry : entries) {
            y -= rowHeight;

            auto row = CCMenu::create();
            row->setContentSize({contentWidth, rowHeight});
            row->setAnchorPoint({0.f, 0.f});
            row->setPosition({0.f, y});

            bool isActive = entry.url == getIndexUrl();
            auto updateCount = getIndexUpdateCount(entry.id);
            float nameX = 4.f;

            if (updateCount > 0) {
                auto updateIcon = CCSprite::createWithSpriteFrameName("updates-available.png"_spr);
                updateIcon->setScale(0.4f);
                updateIcon->setPosition({10.f, rowHeight / 2});
                row->addChild(updateIcon);

                auto updateLabel = CCLabelBMFont::create(
                    std::to_string(updateCount).c_str(),
                    "bigFont.fnt"
                );
                updateLabel->setScale(0.3f);
                updateLabel->setAnchorPoint({0.f, 0.5f});
                updateLabel->setPosition({19.f, rowHeight / 2});
                row->addChild(updateLabel);
                nameX = 36.f;
            }

            auto label = CCLabelBMFont::create(
                (isActive ? ("> " + entry.name) : entry.name).c_str(),
                "bigFont.fnt"
            );
            label->setScale(0.35f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({nameX, rowHeight / 2});
            row->addChild(label);

            auto useBtn = CCMenuItemExt::createSpriteExtra(
                ButtonSprite::create("Use", "goldFont.fnt", getButtonTexture("GJ_button_01.png"), 0.5f),
                [url = entry.url, this](auto) {
                    setIndexUrl(url);
                    this->onClose(nullptr);
                    g_shouldReopenModsList = true;
                    if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
                        if (auto backBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(scene->getChildByIDRecursive("back-button")))
                            backBtn->activate();
                    }
                }
            );
            useBtn->setPosition({contentWidth - 78.f, rowHeight / 2});
            row->addChild(useBtn);

            CCNode* modifySprite = nullptr;
            if (isGeodeTheme())
                modifySprite = createSettingsButtonSprite();
            else
                modifySprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");

            auto modifyBtn = CCMenuItemExt::createSpriteExtra(
                modifySprite,
                [this, entry](auto) { showModifyIndexPopup(entry, [this] { rebuildList(); }); }
            );
            modifyBtn->setScale(isGeodeTheme() ? 0.55f : 0.5f);
            modifyBtn->m_baseScale = modifyBtn->getScale();
            modifyBtn->setPosition({contentWidth - 40.f, rowHeight / 2});
            row->addChild(modifyBtn);

            auto deleteBtn = CCMenuItemExt::createSpriteExtra(
                CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png"),
                [this, id = entry.id](auto) {
                    auto name = readSetting("custom-index-name-" + id, "this index");
                    createQuickPopup(
                        "Delete Index",
                        fmt::format("Are you sure you want to delete <cy>{}</c>?", name),
                        "Cancel", "Delete",
                        [this, id](auto, bool confirmed) {
                            if (confirmed) {
                                deleteCustomIndex(id);
                                invalidateIndexUpdateCache();
                                rebuildList();
                                fetchIndexUpdates([this] { rebuildList(); });
                            }
                        }
                    );
                }
            );
            deleteBtn->setScale(0.5f);
            deleteBtn->m_baseScale = 0.5f;
            deleteBtn->setPosition({contentWidth - 15.f, rowHeight / 2});
            row->addChild(deleteBtn);

            m_scrollLayer->m_contentLayer->addChild(row);
        }

        m_scrollLayer->m_contentLayer->setContentSize({contentWidth, totalHeight});
        m_scrollLayer->scrollToTop();
    }

public:
    static IndexListPopup* create() {
        auto ret = new IndexListPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void showIndexListPopup() {
    IndexListPopup::create()->show();
}

} // namespace opengeode
