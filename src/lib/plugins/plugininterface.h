/* ============================================================
* Falkon - Qt web browser
* Copyright (C) 2010-2016 David Rosca <nowrep@gmail.com>
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
#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QtPlugin>
#include <QWebEnginePage>

#include "qzcommon.h"

class WebPage;

// Headless fork: all UI hooks (menus, input events, windows, settings dialogs)
// have been removed. Plugins can only observe page state and reject
// navigation requests / new windows.
class PluginInterface
{
public:
    enum InitState {
        StartupInitState,
        LateInitState
    };

    virtual ~PluginInterface() = default;

    virtual void init(InitState state, const QString &settingsPath) = 0;
    virtual void unload() = 0;
    virtual bool testPlugin() = 0;

    virtual bool acceptNavigationRequest(WebPage *page, const QUrl &url, QWebEnginePage::NavigationType type, bool isMainFrame) { Q_UNUSED(page); Q_UNUSED(url); Q_UNUSED(type); Q_UNUSED(isMainFrame); return true; }
    virtual bool newWindowRequested(WebPage *page, QWebEngineNewWindowRequest &request) { Q_UNUSED(page) Q_UNUSED(request) return true; }
};

Q_DECLARE_INTERFACE(PluginInterface, "Falkon.Browser.PluginInterface/3.0")

#endif // PLUGININTERFACE_H
