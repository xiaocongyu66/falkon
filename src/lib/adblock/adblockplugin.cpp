/* ============================================================
* Falkon - Qt web browser
* Copyright (C) 2018 David Rosca <nowrep@gmail.com>
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

#include "adblockplugin.h"
#include "adblockmanager.h"
#include "adblocknetworkrequest.h"
#include "adblockscripts.h"
#include "adblockresourceschemehandler.h"

#include "scripts.h"
#include "webpage.h"
#include "pluginproxy.h"
#include "mainapplication.h"
#include "desktopfile.h"

#include <QWebEngineProfile>
#include <QWebEngineNewWindowRequest>

AdBlockPlugin::AdBlockPlugin(QObject *parent)
    : QObject(parent)
{
}

void AdBlockPlugin::init(InitState state, const QString &settingsPath)
{
    Q_UNUSED(settingsPath)

    connect(mApp, &MainApplication::aboutToQuit, AdBlockManager::instance(), &AdBlockManager::save);
    connect(mApp->plugins(), &PluginProxy::webPageCreated, this, &AdBlockPlugin::webPageCreated);
    connect(mApp->plugins(), &PluginProxy::webPageDeleted, this, &AdBlockPlugin::webPageDeleted);
    connect(this, &AdBlockPlugin::newWindowRequestBlocked, AdBlockManager::instance(), &AdBlockManager::requestBlocked);

    m_resourceschemeHandler = new AdblockResourceSchemeHandler(this);
    mApp->webProfile()->installUrlSchemeHandler(QByteArrayLiteral("abp-resource"), m_resourceschemeHandler);

    // LateInitState only matters for per-window UI wiring, which does not exist headless.
    Q_UNUSED(state)
}

void AdBlockPlugin::unload()
{
    mApp->webProfile()->removeUrlSchemeHandler(m_resourceschemeHandler);
    delete m_resourceschemeHandler;
    m_resourceschemeHandler = nullptr;
}

bool AdBlockPlugin::testPlugin()
{
    return true;
}

void AdBlockPlugin::webPageCreated(WebPage *page)
{
    connect(page, &WebPage::loadFinished, this, [=]() {
        AdBlockManager *manager = AdBlockManager::instance();
        if (!manager->isEnabled()) {
            return;
        }
        // Apply global element hiding rules
        const QString elementHiding = manager->elementHidingRules(page->url());
        if (!elementHiding.isEmpty()) {
            page->runJavaScript(Scripts::setCss(elementHiding), WebPage::SafeJsWorld);
        }
        // Apply domain-specific element hiding rules
        const QString siteElementHiding = manager->elementHidingRulesForDomain(page->url());
        if (!siteElementHiding.isEmpty()) {
            page->runJavaScript(Scripts::setCss(siteElementHiding), WebPage::SafeJsWorld);
        }

        /* Apply domain-specific element remove rules */
        const QString siteElementRemove = manager->elementRemoveRulesForDomain(page->url());
        if (!siteElementRemove.isEmpty()) {
            page->runJavaScript(AdBlockScripts::removeRulesScript(siteElementRemove), WebPage::SafeJsWorld);
        }
    });
}

void AdBlockPlugin::webPageDeleted(WebPage *page)
{
    AdBlockManager::instance()->clearBlockedRequestsForUrl(page->url());
}

bool AdBlockPlugin::acceptNavigationRequest(WebPage *page, const QUrl &url, QWebEnginePage::NavigationType type, bool isMainFrame)
{
    Q_UNUSED(type)

    AdBlockManager *manager = AdBlockManager::instance();
    if (isMainFrame) {
        manager->clearBlockedRequestsForUrl(page->url());
    }
    if (url.scheme() == QL1S("abp") && AdBlockManager::instance()->addSubscriptionFromUrl(url)) {
        return false;
    }
    return true;
}

bool AdBlockPlugin::newWindowRequested(WebPage* page, QWebEngineNewWindowRequest &request)
{
    QString ruleFilter;
    QString ruleSubscription;
    QUrl rewriteUrl;
    AdBlockManager *manager = AdBlockManager::instance();
    AdBlockNeworkRequest adBlockRequest = AdBlockNeworkRequest(page->url(), request);

    if (manager->block(adBlockRequest, ruleFilter, ruleSubscription, rewriteUrl)) {
        AdBlockedRequest r;
        r.requestUrl = request.requestedUrl();
        r.firstPartyUrl = page->url();
        r.requestMethod = QByteArrayLiteral("GET");
        r.resourceType = QWebEngineUrlRequestInfo::ResourceTypeMainFrame;
        r.navigationType = QWebEngineUrlRequestInfo::NavigationTypeOther;
        r.rule = ruleFilter;
        Q_EMIT newWindowRequestBlocked(r);

        return false;
    }

    return true;
}
