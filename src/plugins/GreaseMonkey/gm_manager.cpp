/* ============================================================
* GreaseMonkey plugin for Falkon
* Copyright (C) 2012-2018 David Rosca <nowrep@gmail.com>
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
#include "gm_manager.h"
#include "gm_script.h"
#include "gm_downloader.h"
#include "gm_jsobject.h"

#include "webpage.h"
#include "qztools.h"
#include "mainapplication.h"
#include "networkmanager.h"
#include "javascript/externaljsobject.h"

#include <QTimer>
#include <QDir>
#include <QSettings>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>

GM_Manager::GM_Manager(const QString &sPath, QObject* parent)
    : QObject(parent)
    , m_enabled(true)
    , m_settingsPath(sPath)
    , m_jsObject(new GM_JSObject(this))
{
    load();
}

GM_Manager::~GM_Manager()
{
    ExternalJsObject::unregisterExtraObject(m_jsObject);
}

void GM_Manager::downloadScript(const QUrl &url)
{
    auto *downloader = new GM_Downloader(url, this);
    connect(downloader, &GM_Downloader::finished, this, [=](const QString &fileName) {
        bool deleteScript = true;
        auto *script = new GM_Script(this, fileName);
        if (script->isValid()) {
            // Headless build: install scripts unconditionally (no confirm dialog).
            if (!containsScript(script->fullName())) {
                addScript(script);
                deleteScript = false;
            }
            else {
                std::cout << "GreaseMonkey: script already installed: " << qPrintable(script->name()) << std::endl;
            }
        }

        if (deleteScript) {
            delete script;
            QFile(fileName).remove();
        }
    });
}

QString GM_Manager::settingsPath() const
{
    return m_settingsPath;
}

QString GM_Manager::scriptsDirectory() const
{
    return m_settingsPath + QL1S("/greasemonkey");
}

QString GM_Manager::requireScripts(const QStringList &urlList) const
{
    QDir requiresDir(m_settingsPath + QL1S("/greasemonkey/requires"));
    if (!requiresDir.exists() || urlList.isEmpty()) {
        return {};
    }

    QSettings settings(m_settingsPath + QL1S("/greasemonkey/requires/requires.ini"), QSettings::IniFormat);
    settings.beginGroup(QSL("Files"));

    QString script;

    for (const QString &url : urlList) {
        if (settings.contains(url)) {
            QString fileName = settings.value(url).toString();
            if (!QFileInfo(fileName).isAbsolute()) {
                fileName = m_settingsPath + QL1S("/greasemonkey/requires/") + fileName;
            }
            const QString data = QzTools::readAllFileContents(fileName).trimmed();
            if (!data.isEmpty()) {
                script.append(data + QL1C('\n'));
            }
        }
    }
    settings.endGroup();

    return script;
}

QString GM_Manager::bootstrapScript() const
{
    return m_bootstrapScript;
}

QString GM_Manager::valuesScript() const
{
    return m_valuesScript;
}

void GM_Manager::loadScripts()
{
    QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();

    for (const auto &gmScript : std::as_const(m_scripts)) {
        if (!gmScript->isEnabled()) {
            return;
        }

        if (gmScript->startAt() == GM_Script::ContextMenu) {
            continue;
        }

        if (!canRunScript(gmScript)) {
            continue;
        }

        collection->insert(gmScript->webScript());
    }
}

void GM_Manager::unloadScripts()
{
    QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();

    for (const auto &gmScript : std::as_const(m_scripts)) {
        const auto scripts = collection->find(gmScript->fullName());
        for (const auto &script : scripts) {
            collection->remove(script);
        }
    }
}

void GM_Manager::unloadPlugin()
{
    // Save settings
    QSettings settings(m_settingsPath + QSL("/extensions.ini"), QSettings::IniFormat);
    settings.beginGroup(QSL("GreaseMonkey"));
    settings.setValue(QSL("enabled"), m_enabled);
    settings.setValue(QSL("disabledScripts"), m_disabledScripts);
    settings.endGroup();

    unloadScripts();

    for (GM_Script *gmScript : std::as_const(m_scripts)) {
        delete gmScript;
    }
    m_scripts.clear();
    m_contextMenuScripts.clear();
}

bool GM_Manager::isEnabled() const
{
    return m_enabled;
}

QList<GM_Script*> GM_Manager::allScripts() const
{
    return m_scripts;
}

QList<GM_Script*> GM_Manager::contextMenuScripts() const
{
    return m_contextMenuScripts;
}

bool GM_Manager::containsScript(const QString &fullName) const
{
    for (GM_Script* script : std::as_const(m_scripts)) {
        if (fullName == script->fullName()) {
            return true;
        }
    }

    return false;
}

bool GM_Manager::canRunScript(GM_Script *script) const
{
    if (script == nullptr) {
        return false;
    }

    if (   (script->runIn() == GM_Script::RunIn::NormalTabs && mApp->isPrivate())
        || (script->runIn() == GM_Script::RunIn::IncognitoTabs && !mApp->isPrivate())
    ) {
        return false;
    }

    return true;
}

void GM_Manager::enableScript(GM_Script* script)
{
    script->setEnabled(true);
    m_disabledScripts.removeOne(script->fullName());

    if (script->startAt() == GM_Script::ContextMenu) {
        m_contextMenuScripts.append(script);
    }
    else if (isEnabled() && canRunScript(script)) {
        QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();
        collection->insert(script->webScript());
    }
}

void GM_Manager::disableScript(GM_Script* script)
{
    script->setEnabled(false);
    m_disabledScripts.append(script->fullName());

    if (script->startAt() == GM_Script::ContextMenu) {
        m_contextMenuScripts.removeOne(script);
    }
    else {
        QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();
        const auto scripts = collection->find(script->fullName());
        for (const auto &script : scripts) {
            collection->remove(script);
        }
    }
}

bool GM_Manager::addScript(GM_Script* script)
{
    if (!script || !script->isValid()) {
        return false;
    }

    m_scripts.append(script);
    connect(script, &GM_Script::scriptChanged, this, &GM_Manager::scriptChanged);

    if (script->startAt() == GM_Script::ContextMenu) {
        m_contextMenuScripts.append(script);
    }
    else if (isEnabled() && canRunScript(script)) {
        QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();
        collection->insert(script->webScript());
    }

    Q_EMIT scriptsChanged();
    return true;
}

bool GM_Manager::removeScript(GM_Script* script, const bool removeFile)
{
    if (!script) {
        return false;
    }

    m_scripts.removeOne(script);

    if (script->startAt() == GM_Script::ContextMenu) {
        m_contextMenuScripts.removeOne(script);
    }
    else {
        QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();
        const auto scripts = collection->find(script->fullName());
        for (const auto &script : scripts) {
            collection->remove(script);
        }
    }

    m_disabledScripts.removeOne(script->fullName());

    if (removeFile) {
        QFile::remove(script->fileName());
        delete script;
    }

    Q_EMIT scriptsChanged();
    return true;
}

#include <iostream>

void GM_Manager::showNotification(const QString &message, const QString &title)
{
    Q_UNUSED(title)
    std::cout << "GreaseMonkey: " << qPrintable(message) << std::endl;
}

void GM_Manager::load()
{
    QDir gmDir(m_settingsPath + QL1S("/greasemonkey"));
    if (!gmDir.exists()) {
        gmDir.mkdir(m_settingsPath + QL1S("/greasemonkey"));
    }

    if (!gmDir.exists(QSL("requires"))) {
        gmDir.mkdir(QSL("requires"));
    }

    m_bootstrapScript = QzTools::readAllFileContents(QSL(":gm/data/bootstrap.min.js"));
    m_valuesScript = QzTools::readAllFileContents(QSL(":gm/data/values.min.js"));

    QSettings settings(m_settingsPath + QL1S("/extensions.ini"), QSettings::IniFormat);
    settings.beginGroup(QSL("GreaseMonkey"));
    m_enabled = settings.value(QSL("enabled"), m_enabled).toBool();
    m_disabledScripts = settings.value(QSL("disabledScripts"), QStringList()).toStringList();
    settings.endGroup();

    const auto fileNames = gmDir.entryList(QStringList(QSL("*.js")), QDir::Files);
    for (const QString &fileName : fileNames) {
        const QString absolutePath = gmDir.absoluteFilePath(fileName);
        auto* script = new GM_Script(this, absolutePath);

        if (!script->isValid()) {
            delete script;
            continue;
        }

        m_scripts.append(script);

        if (m_disabledScripts.contains(script->fullName())) {
            script->setEnabled(false);
        }
        else if (script->startAt() == GM_Script::ContextMenu) {
            m_contextMenuScripts.append(script);
        }
        else if (isEnabled() && canRunScript(script)) {
            mApp->webProfile()->scripts()->insert(script->webScript());
        }
    }

    m_jsObject->setSettingsFile(m_settingsPath + QSL("/greasemonkey/values.ini"));
    ExternalJsObject::registerExtraObject(QSL("greasemonkey"), m_jsObject);
}

void GM_Manager::scriptChanged()
{
    auto *script = qobject_cast<GM_Script*>(sender());
    if (!script) {
        return;
    }

    QWebEngineScriptCollection *collection = mApp->webProfile()->scripts();
    const auto scripts = collection->find(script->fullName());
    for (const auto &script : scripts) {
        collection->remove(script);
    }
    for (const auto &cmScript : std::as_const(m_contextMenuScripts)) {
        if (cmScript->fullName() == script->fullName()) {
            m_contextMenuScripts.removeOne(cmScript);
        }
    }

    if (script->startAt() == GM_Script::ContextMenu) {
        m_contextMenuScripts.append(script);
    }
    else if (isEnabled() && canRunScript(script)) {
        collection->insert(script->webScript());
    }
}

bool GM_Manager::canRunOnScheme(const QString &scheme)
{
    return (scheme == QLatin1String("http") || scheme == QLatin1String("https")
            || scheme == QLatin1String("data") || scheme == QLatin1String("ftp"));
}


void GM_Manager::setEnabled(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    Q_EMIT enabledChanged(enabled);

    QSettings settings(m_settingsPath + QL1S("/extensions.ini"), QSettings::IniFormat);
    settings.beginGroup(QSL("GreaseMonkey"));
    settings.setValue(QSL("enabled"), m_enabled);
    settings.endGroup();

    if (m_enabled) {
        loadScripts();
    } else {
        unloadScripts();
    }
}

void GM_Manager::mainWindowCreated(BrowserWindow* window)
{
    Q_UNUSED(window)
}

void GM_Manager::mainWindowDeleted(BrowserWindow* window)
{
    Q_UNUSED(window)
}
