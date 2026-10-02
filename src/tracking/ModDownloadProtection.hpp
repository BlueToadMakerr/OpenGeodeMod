#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <string>

namespace opengeode {

bool isGeodeModDownloadRequest(std::string const& url, std::string& modID);
bool blockAlreadyUpdatedModDownload(geode::utils::web::WebRequest& request, std::string const& modID);
void showAlreadyUpdatedPopup(std::string const& modID);

} // namespace opengeode
