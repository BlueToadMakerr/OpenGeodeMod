#include "FilterPopup.hpp"
#include "ModsListUtils.hpp"
#include "PopupSectionUtils.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <array>

using namespace geode::prelude;

namespace opengeode {

class FilterPopup : public Popup {
protected:
    TextInput* m_platformInput = nullptr;
    TextInput* m_geodeInput = nullptr;
    TextInput* m_gdInput = nullptr;

    std::array<CCMenuItemToggler*, 4> m_statusButtons{};
    ModStatus m_selectedStatus = ModStatus::Accepted;

    struct StatusInfo {
        ModStatus status;
        char const* label;
        ccColor3B color;
    };

    static constexpr std::array<StatusInfo, 4> STATUS_INFO = {{
        { ModStatus::Accepted, "Accepted", { 0, 220, 70 } },
        { ModStatus::Unlisted, "Unlisted", { 255, 150, 0 } },
        { ModStatus::Pending,  "Pending",  { 255, 220, 0 } },
        { ModStatus::Rejected, "Rejected", { 235, 40, 40 } },
    }};

    static CCNode* createStatusVisual(StatusInfo const& info, bool selected) {
        auto root = CCNode::create();
        if (!root)
            return nullptr;

        auto label = CCLabelBMFont::create(info.label, "bigFont.fnt");
        if (!label)
            return nullptr;

        // Keep the label compact so all four status tags fit comfortably in
        // one centered row, just like the native Geode tag selector.
        label->setScale(.28f);

        float const textWidth = label->getContentSize().width * label->getScaleX();
        float const width = textWidth + 10.f;
        float const height = 20.f;

        root->setContentSize({width, height});
        root->setAnchorPoint({.5f, .5f});

        // The status color is a flat rectangle underneath the text. Using a
        // normal CCLayerColor avoids depending on the internal sprite-frame
        // name for cc_2x2_white_image.png and keeps the dimensions finite.
        auto bg = CCLayerColor::create(ccc4(info.color.r, info.color.g, info.color.b, 255), width, height);
        if (!bg)
            return nullptr;

        bg->setOpacity(selected ? 255 : 95);
        bg->setPosition({0.f, 0.f});
        root->addChild(bg, 0);

        label->setAnchorPoint({.5f, .5f});
        label->setPosition({width / 2.f, height / 2.f});
        label->setOpacity(selected ? 255 : 180);
        root->addChild(label, 1);

        return root;
    }

    CCMenuItemToggler* createStatusButton(StatusInfo const& info) {
        auto off = createStatusVisual(info, false);
        auto on = createStatusVisual(info, true);
        if (!off || !on)
            return nullptr;

        auto toggle = CCMenuItemToggler::create(
            off,
            on,
            this,
            menu_selector(FilterPopup::onSelectStatus)
        );
        if (!toggle)
            return nullptr;

        // Like Geode's own tag filter, this row is controlled by our
        // selection state rather than CCMenuItemToggler toggling itself.
        toggle->m_notClickable = true;

        toggle->setUserObject(
            "status",
            CCString::create(statusToString(info.status))
        );
        return toggle;
    }

    void updateStatusButtons() {
        for (size_t i = 0; i < STATUS_INFO.size(); ++i) {
            if (m_statusButtons[i]) {
                m_statusButtons[i]->toggle(
                    m_selectedStatus == STATUS_INFO[i].status
                );
            }
        }
    }

    void onSelectStatus(CCObject* sender) {
        auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
        if (!toggle)
            return;

        auto value = static_cast<CCString*>(toggle->getUserObject("status"));
        if (!value)
            return;

        m_selectedStatus = statusFromString(value->getCString());
        updateStatusButtons();
    }

    void onReset(CCObject*) {
        m_platformInput->setString("");
        m_geodeInput->setString("");
        m_gdInput->setString("");
        m_selectedStatus = ModStatus::Accepted;
        updateStatusButtons();
    }

    void onClose(CCObject* sender) override {
        // 1. Save configurations and update UI elements
        auto& config = getCurrentTabConfig();
        config.platform = m_platformInput->getString();
        config.geodeVersion = m_geodeInput->getString();
        config.gdVersion = m_gdInput->getString();
        config.status = m_selectedStatus;

        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(
                    scene->getChildByIDRecursive("index-filter-button")
                )) {
                btn->setNormalImage(buildFilterButtonSprite());
            }
        }

        triggerModsListReload();

        // 2. Call the Geode base class handler to safely close the layer
        Popup::onClose(sender);
    }

    bool init() override {
        // Compact Geode-style popup: a small Status section at the top and a
        // separate Parameters section at the bottom.
        if (!Popup::init(350.f, 285.f))
            return false;

        this->setTitle("Browse Filters");

        auto const& config = getCurrentTabConfig();
        m_selectedStatus = config.status;

        // ─────────────────────────────────────────────
        // Status section
        // ─────────────────────────────────────────────
        auto statusContainer = createSectionContainer({310.f, 35.f});

        auto resetSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
        auto resetBtn = CCMenuItemSpriteExtra::create(
            resetSpr,
            this,
            menu_selector(FilterPopup::onReset)
        );
        resetBtn->setScale(.45f);
        resetBtn->m_baseScale = .45f;

        auto statusTitle = createSectionTitle("Status", resetBtn, statusContainer->getContentWidth());
        statusContainer->addChildAtPosition(
            statusTitle,
            Anchor::TopLeft,
            ccp(0, 4)
        );

        // Exactly one row containing all four status buttons.
        auto statusMenu = CCMenu::create();
        statusMenu->setContentSize({292.f, 24.f});
        statusMenu->setLayout(
            RowLayout::create()
                ->setAutoScale(false)
                ->setGap(3.f)
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );

        for (size_t i = 0; i < STATUS_INFO.size(); ++i) {
            auto button = createStatusButton(STATUS_INFO[i]);
            if (!button)
                continue;

            m_statusButtons[i] = button;
            statusMenu->addChild(button);
        }

        statusMenu->updateLayout();
        statusContainer->addChildAtPosition(
            statusMenu,
            Anchor::Center,
            ccp(0, 0)
        );

        // Same placement convention as the native FiltersPopup.cpp: section
        // title at the top-left with the reset icon at the top-right.
        m_mainLayer->addChildAtPosition(
            statusContainer,
            Anchor::Center,
            ccp(0, 75)
        );

        // ─────────────────────────────────────────────
        // Parameters section
        // ─────────────────────────────────────────────
        auto parametersContainer = createSectionContainer({310.f, 130.f});

        auto parametersTitle = createSectionTitle(
            "Parameters",
            nullptr,
            parametersContainer->getContentWidth()
        );
        parametersContainer->addChildAtPosition(
            parametersTitle,
            Anchor::TopLeft,
            ccp(0, 4)
        );

        float centerX = parametersContainer->getContentWidth() / 2.f;
        float top = parametersContainer->getContentHeight() - 29.f;

        m_platformInput = TextInput::create(
            250.f,
            "platform, e.g. win",
            "chatFont.fnt"
        );
        m_platformInput->setString(config.platform);
        m_platformInput->setPosition({centerX, top});
        parametersContainer->addChild(m_platformInput);

        m_geodeInput = TextInput::create(
            250.f,
            "Geode version, e.g. 5.0.0",
            "chatFont.fnt"
        );
        m_geodeInput->setString(config.geodeVersion);
        m_geodeInput->setPosition({centerX, top - 38.f});
        parametersContainer->addChild(m_geodeInput);

        m_gdInput = TextInput::create(
            250.f,
            "GD version, e.g. 2.2074",
            "chatFont.fnt"
        );
        m_gdInput->setString(config.gdVersion);
        m_gdInput->setPosition({centerX, top - 76.f});
        parametersContainer->addChild(m_gdInput);

        m_mainLayer->addChildAtPosition(
            parametersContainer,
            Anchor::Bottom,
            ccp(0, 105)
        );

        auto applySpr = ButtonSprite::create(
            "OK",
            "goldFont.fnt",
            "GJ_button_01.png",
            .7f
        );
        auto applyBtn = CCMenuItemSpriteExtra::create(
            applySpr,
            this,
            menu_selector(FilterPopup::onClose)
        );
        m_buttonMenu->addChildAtPosition(
            applyBtn,
            Anchor::Bottom,
            ccp(0, 18)
        );

        updateStatusButtons();
        return true;
    }

public:
    static FilterPopup* create() {
        auto ret = new FilterPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }

        delete ret;
        return nullptr;
    }
};

void showFilterPopup() {
    if (auto popup = FilterPopup::create())
        popup->show();
}

} // namespace opengeode
