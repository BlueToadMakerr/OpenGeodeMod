#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace geode::prelude;

namespace opengeode {
namespace {

constexpr char const* kPatchedFlag = "opengeode-comments-polish/patched";
constexpr char const* kLastStatusKey = "opengeode-comments-polish/last-status";

bool isLoadingStatus(std::string const& text) {
    for (auto const* prefix : {
        "Loading ", "Posting...", "Saving...", "Updating lock...",
        "Uploading attachments...", "Deleting comment...", "Removing attachment..."
    }) {
        if (text.starts_with(prefix)) return true;
    }
    return false;
}

bool isErrorStatus(std::string const& text) {
    for (auto const* part : {
        "failed", "Failed", "cannot", "Cannot", "does not", "locked",
        "Locked", "require", "Require", "Log in", "Select a version",
        "Write something", "Request failed", "HTTP "
    }) {
        if (text.find(part) != std::string::npos) return true;
    }
    return false;
}

void walk(CCNode* node, std::function<void(CCNode*)> const& fn) {
    if (!node) return;
    fn(node);
    for (auto child : node->getChildrenExt())
        walk(child, fn);
}

CCNode* findCommentsRoot(CCNode* scene) {
    auto scroll = scene->getChildByIDRecursive("opengeode-comments-scroll"_spr);
    if (!scroll) return nullptr;
    auto area = scroll->getParent();
    return area ? area->getParent() : nullptr;
}

void resetButton(CCMenuItemSpriteExtra* button, float baseScale) {
    if (!button) return;
    button->m_baseScale = baseScale;
    button->setScale(baseScale);
}

void resetButtons(CCNode* node, float baseScale) {
    walk(node, [baseScale](CCNode* child) {
        if (auto button = typeinfo_cast<CCMenuItemSpriteExtra*>(child))
            resetButton(button, baseScale);
    });
}

void shrinkCommentsChrome(CCNode* root) {
    if (!root || root->getUserFlag(kPatchedFlag)) return;

    CCNode* top = nullptr;
    CCNode* bottom = nullptr;
    CCNode* commentsArea = nullptr;

    for (auto child : root->getChildrenExt()) {
        auto h = child->getContentHeight();
        if (std::abs(h - 34.f) < 1.5f) top = child;
        else if (std::abs(h - 44.f) < 1.5f) bottom = child;
        else if (child->getChildByIDRecursive("opengeode-comments-scroll"_spr))
            commentsArea = child;
    }

    if (!top || !bottom || !commentsArea) return;

    constexpr float topHeight = 30.f;
    constexpr float bottomHeight = 39.f;
    constexpr float inset = 2.f;
    constexpr float gap = 2.f;

    top->setContentSize({top->getContentWidth(), topHeight});
    bottom->setContentSize({bottom->getContentWidth(), bottomHeight});

    auto rootHeight = root->getContentHeight();
    auto middleHeight = std::max(
        1.f,
        rootHeight - (topHeight + inset) - (bottomHeight + inset) - gap * 2.f
    );
    commentsArea->setContentSize({commentsArea->getContentWidth(), middleHeight});

    if (auto scroll = commentsArea->getChildByIDRecursive("opengeode-comments-scroll"_spr)) {
        scroll->setContentSize({commentsArea->getContentWidth(), middleHeight});
    }

    // The old button code scaled the display nodes but left m_baseScale at its
    // default. CCMenuItemSpriteExtra uses m_baseScale when it unselects, so it
    // could grow back to its default size. Reset the return scale as well.
    resetButtons(top, .50f);
    resetButtons(bottom, .60f);

    root->setUserFlag(kPatchedFlag);
    root->updateLayout();
}

class CommentsUiPolishWatcher : public CCNode {
    std::string m_lastStatus;
    Ref<Notification> m_notification;
    Ref<CCNode> m_lastRoot;

    void clearNotification() {
        if (m_notification) {
            m_notification->cancel();
            m_notification = nullptr;
        }
    }

    void showStatus(std::string const& text) {
        if (text.empty() || text == m_lastStatus) return;
        m_lastStatus = text;
        clearNotification();

        if (isLoadingStatus(text)) {
            m_notification = Notification::create(text, NotificationIcon::Loading, 0.f);
        }
        else {
            m_notification = Notification::create(
                text,
                isErrorStatus(text) ? NotificationIcon::Error : NotificationIcon::Success,
                1.f
            );
        }
        if (m_notification) m_notification->show();
    }

    void inspectStatus(CCNode* root) {
        CCLabelBMFont* status = nullptr;
        walk(root, [&status](CCNode* node) {
            if (status) return;
            auto label = typeinfo_cast<CCLabelBMFont*>(node);
            if (!label || !label->isVisible()) return;
            if (std::abs(label->getScale() - .20f) > .035f) return;
            if (std::string(label->getFntFile()) != "chatFont.fnt") return;
            auto text = std::string(label->getString());
            if (text.empty()) return;
            status = label;
        });

        if (!status) {
            if (!m_lastStatus.empty()) {
                m_lastStatus.clear();
                clearNotification();
            }
            return;
        }

        auto text = std::string(status->getString());
        status->setVisible(false);
        showStatus(text);
    }

    void shrinkAttachmentViewer(CCNode* scene) {
        // AttachmentImagePopup currently uses a 350x300 Popup. On short aspect
        // ratios that clips its top/bottom. Find the title and shrink the popup
        // itself so the whole Geode Popup remains inside the screen.
        CCLabelBMFont* title = nullptr;
        walk(scene, [&title](CCNode* node) {
            if (title) return;
            auto label = typeinfo_cast<CCLabelBMFont*>(node);
            if (label && std::string(label->getString()) == "Attachment")
                title = label;
        });
        if (!title) return;

        auto node = title->getParent();
        while (node && node->getParent()) {
            auto size = node->getContentSize();
            if (size.width >= 345.f && size.width <= 355.f &&
                size.height >= 295.f && size.height <= 305.f) {
                node->setScale(.92f);
                return;
            }
            node = node->getParent();
        }
    }

    void check(float) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        auto root = findCommentsRoot(scene);
        if (root) {
            if (m_lastRoot != root) {
                m_lastRoot = root;
                m_lastStatus.clear();
                clearNotification();
            }
            shrinkCommentsChrome(root);
            inspectStatus(root);
        }
        else if (m_lastRoot) {
            m_lastRoot = nullptr;
            m_lastStatus.clear();
            clearNotification();
        }

        shrinkAttachmentViewer(scene);
    }

    bool init() {
        if (!CCNode::init()) return false;
        setID("opengeode-comments-polish-watcher"_spr);
        schedule(schedule_selector(CommentsUiPolishWatcher::check), .10f);
        return true;
    }

public:
    static CommentsUiPolishWatcher* create() {
        auto ret = new CommentsUiPolishWatcher();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

} // namespace

$on_mod(Loaded) {
    SceneEvent().listen([](CCScene* scene) {
        if (!scene) return ListenerResult::Propagate;
        if (scene->getChildByID("opengeode-comments-polish-watcher"_spr))
            return ListenerResult::Propagate;
        if (auto watcher = CommentsUiPolishWatcher::create())
            scene->addChild(watcher);
        return ListenerResult::Propagate;
    }).leak();
}

} // namespace opengeode
