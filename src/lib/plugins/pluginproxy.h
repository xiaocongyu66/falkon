/* ============================================================
* Falkon - Qt web browser
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
#ifndef PLUGINPROXY_H
#define PLUGINPROXY_H

#include "plugins.h"
#include "qzcommon.h"

#include <QWebEnginePage>

class WebPage;

class FALKON_EXPORT PluginProxy : public Plugins
{
    Q_OBJECT
public:
    explicit PluginProxy(QObject *parent = nullptr);

    bool acceptNavigationRequest(WebPage *page, const QUrl &url, QWebEnginePage::NavigationType type, bool isMainFrame);
    bool newWindowRequested(WebPage *page, QWebEngineNewWindowRequest &request);

    void emitWebPageCreated(WebPage* page);
    void emitWebPageDeleted(WebPage* page);

Q_SIGNALS:
    void webPageCreated(WebPage* page);
    void webPageDeleted(WebPage* page);
};

#endif // PLUGINPROXY_H
