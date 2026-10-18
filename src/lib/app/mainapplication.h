/* ============================================================
* Falkon - Qt web browser (headless render fork)
* Copyright (C) 2010-2018 David Rosca <nowrep@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */
#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#define mApp MainApplication::instance()

#include <QList>

#include "qtsingleapplication/qtsingleapplication.h"
#include "qzcommon.h"

class QWebEngineProfile;
class QWebEngineSettings;
class QWebEngineDownloadRequest;

class History;
class AutoFill;
class Bookmarks;
class CookieJar;
class PluginProxy;
class NetworkManager;
class UserAgentManager;
class SearchEnginesManager;
class ProtocolHandlerManager;
class SiteSettingsManager;

// Headless-only application: no browser windows, no sessions, no downloads UI.
// The sole job is runHeadlessRender() - load a URL, wait for readiness,
// grab the view and exit with a status code.
class FALKON_EXPORT MainApplication : public QtSingleApplication
{
    Q_OBJECT

public:
    explicit MainApplication(int &argc, char** argv);
    ~MainApplication() override;

    void setChromiumFlags();

    bool isClosing() const;
    bool isPrivate() const;
    bool isPortable() const;

    void reloadSettings();

    History* history();
    Bookmarks* bookmarks();

    AutoFill* autoFill();
    CookieJar* cookieJar();
    PluginProxy* plugins();

    NetworkManager* networkManager();
    UserAgentManager* userAgentManager();
    SearchEnginesManager* searchEnginesManager();
    ProtocolHandlerManager *protocolHandlerManager();
    SiteSettingsManager* siteSettingsManager();
    QWebEngineProfile* webProfile() const;
    QWebEngineSettings *webSettings() const;

    static MainApplication* instance();

    static bool isTestModeEnabled();
    static void setTestModeEnabled(bool enabled);

public Q_SLOTS:
    void reloadUserStyleSheet();

Q_SIGNALS:
    void settingsReloaded();

private Q_SLOTS:
    void saveSettings();
    void messageReceived(const QString &message);

private:
    void loadSettings();

    bool setupWebEngineProfile(const QString &startProfile);

    void setupUserScripts();
    void setUserStyleSheet(const QString &filePath);

    void registerAllowedSchemes();
    void initNetworkInformation();

    void runHeadlessRender();
    bool m_headlessRequested = false;
    QUrl m_headlessUrl;

    bool m_isPrivate;
    bool m_isPortable;
    bool m_isClosing;
    bool m_isStartingAfterCrash;

    History* m_history;
    Bookmarks* m_bookmarks;

    AutoFill* m_autoFill;
    CookieJar* m_cookieJar;
    PluginProxy* m_plugins;

    NetworkManager* m_networkManager;
    UserAgentManager* m_userAgentManager;
    SearchEnginesManager* m_searchEnginesManager;
    ProtocolHandlerManager *m_protocolHandlerManager;
    SiteSettingsManager* m_siteSettingsManager;
    QWebEngineProfile* m_webProfile;
};

#endif // MAINAPPLICATION_H
