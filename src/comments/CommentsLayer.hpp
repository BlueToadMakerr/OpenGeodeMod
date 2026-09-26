#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include "CommentsTypes.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opengeode {

class CommentsLayer : public cocos2d::CCLayer {
    std::string m_modID;
        CCNode* m_textArea = nullptr;
        CommentState m_state;
    
        async::TaskHolder<web::WebResponse> m_requestTask;
        std::vector<std::filesystem::path> m_pendingFiles;
        std::vector<int> m_removedAttachments;
    
        int m_editingCommentID = 0;
    
        CCMenuItemSpriteExtra* m_versionButton = nullptr;
        CCLabelBMFont* m_attachmentCountLabel = nullptr;
        CCLabelBMFont* m_lockLabel = nullptr;
        float m_lockLabelMaxWidth = 200.f;
        CCMenu* m_lockControls = nullptr;
        CCMenuItemSpriteExtra* m_exitButton = nullptr;
        CCMenuItemSpriteExtra* m_attachButton = nullptr;
        TextInput* m_input = nullptr;
        CCMenuItemSpriteExtra* m_sendButton = nullptr;
        CCNode* m_commentsContainer = nullptr;
        CCNode* m_bottom = nullptr;
        std::unordered_map<int, int> m_attachmentOffsets;
    
        bool init(std::string modID, CCNode* textArea)

    std::string m_modID; CCNode* m_textArea = nullptr; CommentState m_state; async::TaskHolder<web::WebResponse> m_requestTask; std::vector<std::filesystem::path> m_pendingFiles; std::vector<int> m_removedAttachments; int m_editingCommentID = 0; CCMenuItemSpriteExtra* m_versionButton = nullptr; CCLabelBMFont* m_attachmentCountLabel = nullptr; CCLabelBMFont* m_lockLabel = nullptr; float m_lockLabelMaxWidth = 200.f; CCMenu* m_lockControls = nullptr; CCMenuItemSpriteExtra* m_exitButton = nullptr; CCMenuItemSpriteExtra* m_attachButton = nullptr; TextInput* m_input = nullptr; CCMenuItemSpriteExtra* m_sendButton = nullptr; CCNode* m_commentsContainer = nullptr; CCNode* m_bottom = nullptr; std::unordered_map<int, int> m_attachmentOffsets; bool init(std::string modID, CCNode* textArea);
    void updateBottomLayout();
    void request( std::string method, std::string path, std::function<void(web::WebResponse)> callback );
    void load();
    void loadMod();
    void loadSelectedVersion();
    void parseComments(matjson::Value const& json);
    bool canComment() const;
    bool canUploadAttachments() const;
    void showComment(std::string const& text);
    void rebuild();
    void addAvatar(CCNode* avatar, CommentData const& comment);
    void showAttachmentImage(std::string const& url);
    void showVersionPicker();
    void showAttachmentsPopup();
    void showLockPicker();
    void setLock(std::string value);
    void pickAttachments();
    void submitComment();
    void updateExistingComment(std::string const& text);
    void finishCommentAttachments(int commentID, bool editing);
    void uploadAttachments(int commentID, std::function<void()> finish);
    void removeNextAttachment(int commentID, std::function<void()> finish);
    void beginEdit(CommentData const& comment);
    void exitEdit();
    void toggleAttachmentRemoval(int attachmentID);
    void removePendingFile(std::filesystem::path const& path);
    void deleteComment(int id);

public:
    public: static CommentsLayer* create( std::string modID, CCNode* textArea );
};

} // namespace opengeode
