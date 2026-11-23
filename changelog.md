# Unreleased
- Fixed Geode's own info (loader versions, the geode.loader page) being redirected to the selected index
- Ported to Geode v6 (`asp::fs` filesystem, `geode::Label`, new `MDTextArea` API, `TextInput` changes)
- Fixed the mod ID lookup (Versions, More and Comments buttons) breaking on Geode's new label type
- Fixed "already updated" detection failing outside Android (hard-coded logo path) and its per-frame texture cache scan
- Fixed a failed update download deleting the installed mod before the new file was written
- Fixed "Update All" hanging when the same update was already downloading
- Fixed the Updates button doing nothing while a background refresh was running
- Fixed crashes when closing popups mid-download or on oversized version numbers from an index
- Fixed a leak in comment attachment uploads, and unreadable attachments now report an error

# v1.0.0
Welcome to the underground
