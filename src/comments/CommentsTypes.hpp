#pragma once
#include <string>
#include <vector>
namespace opengeode {
    struct CommentAttachment {
        int id = 0;
        std::string url;
        std::string filename;
    };
    struct CommentData {
        int id = 0;
        std::string body;
        int authorID = 0;
        std::string username;
        std::string pfp;
        bool canEdit = false;
        bool canDelete = false;
        std::vector < CommentAttachment > attachments;
    };
    struct CommentState {
        std::vector < CommentData > comments;
        std::vector < std::string > versions;
        std::vector < int > modDeveloperIDs;
        std::string selectedVersion;
        std::string lock = "none";
        int lockedBy = 0;
        std::string lockedByName;
        int currentDeveloperID = 0;
        bool currentDeveloperAdmin = false;
        bool currentDeveloperVerified = false;
        bool currentDeveloperModDeveloper = false;
        bool loggedIn = false;
    };
}
// namespace opengeode
