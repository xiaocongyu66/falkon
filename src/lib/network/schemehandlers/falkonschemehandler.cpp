/* ============================================================
* Falkon - Qt web browser
* Copyright (C) 2010-2018 David Rosca <nowrep@gmail.com>
* Copyright (C) 2020-2025 Juraj Oravec <jurajoravec@mailo.com>
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
#include "falkonschemehandler.h"
#include "qztools.h"
#include "mainapplication.h"
#include "settings.h"
#include "datapaths.h"
#include "../config.h"

#include <QTimer>
#include <QSettings>
#include <QUrlQuery>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QtWebEngineCoreVersion>
#include <QMetaType>

static QString authorString(const Qz::AuthorInfo &author)
{
    return QSL("&copy; %1-%2 %3 &lt;<a href=\"mailto:%2\">%4</a>&gt;<br>").arg(
        QString::number(author.copyrightBegin),
        QString::number(author.copyrightEnd),
        author.name,
        author.email
    );
}

FalkonSchemeHandler::FalkonSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

void FalkonSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    if (handleRequest(job)) {
        return;
    }

    QStringList knownPages;
    knownPages << QSL("about") << QSL("start") << QSL("config");

    if (knownPages.contains(job->requestUrl().path()))
        job->reply(QByteArrayLiteral("text/html"), new FalkonSchemeReply(job, job));
    else
        job->fail(QWebEngineUrlRequestJob::UrlInvalid);
}

bool FalkonSchemeHandler::handleRequest(QWebEngineUrlRequestJob *job)
{
    if (job->requestUrl().path() == QL1S("reportbug")) {
        job->redirect(QUrl(QString::fromLatin1(Qz::BUGSADDRESS)));
        return true;
    }

    return false;
}

FalkonSchemeReply::FalkonSchemeReply(QWebEngineUrlRequestJob *job, QObject *parent)
    : QIODevice(parent)
    , m_loaded(false)
    , m_job(job)
{
    m_pageName = m_job->requestUrl().path();
    loadPage();
}

void FalkonSchemeReply::loadPage()
{
    if (m_loaded)
        return;

    QString contents;

    if (m_pageName == QLatin1String("about")) {
        contents = aboutPage();
    } else if (m_pageName == QLatin1String("start")) {
        contents = startPage();
    } else if (m_pageName == QLatin1String("config")) {
        contents = configPage();
    }

    QMutexLocker lock(&m_mutex);
    m_buffer.setData(contents.toUtf8());
    m_buffer.open(QIODevice::ReadOnly);
    lock.unlock();

    Q_EMIT readyRead();

    m_loaded = true;
}

qint64 FalkonSchemeReply::bytesAvailable() const
{
    QMutexLocker lock(&m_mutex);
    return m_buffer.bytesAvailable();
}

qint64 FalkonSchemeReply::readData(char *data, qint64 maxSize)
{
    QMutexLocker lock(&m_mutex);
    return m_buffer.read(data, maxSize);
}

qint64 FalkonSchemeReply::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

QString FalkonSchemeReply::startPage()
{
    static QString sPage;

    if (!sPage.isEmpty()) {
        return sPage;
    }

    sPage.append(QzTools::readAllFileContents(QSL(":html/start.html")));
    sPage.replace(QLatin1String("%ABOUT-IMG%"), QSL("qrc:icons/other/startpage.svg"));
    sPage.replace(QLatin1String("%ABOUT-IMG-DARK%"), QSL("qrc:icons/other/startpage-dark.svg"));

    sPage.replace(QLatin1String("%TITLE%"), tr("Start Page"));
    sPage.replace(QLatin1String("%BUTTON-LABEL%"), tr("Search on Web"));
    sPage.replace(QLatin1String("%SEARCH-BY%"), tr("Search results provided by DuckDuckGo"));
    sPage.replace(QLatin1String("%WWW%"), QString::fromLatin1(Qz::WIKIADDRESS));
    sPage.replace(QLatin1String("%ABOUT-FALKON%"), tr("About Falkon"));
    sPage.replace(QLatin1String("%PRIVATE-BROWSING%"), mApp->isPrivate() ? tr("<h1>Private Browsing</h1>") : QString());
    sPage = QzTools::applyDirectionToPage(sPage);

    return sPage;
}

QString FalkonSchemeReply::aboutPage()
{
    static QString aPage;

    if (aPage.isEmpty()) {
        aPage.append(QzTools::readAllFileContents(QSL(":html/about.html")));
        aPage.replace(QLatin1String("%ABOUT-IMG%"), QSL("qrc:icons/other/about.svg"));
        aPage.replace(QLatin1String("%ABOUT-IMG-DARK%"), QSL("qrc:icons/other/about-dark.svg"));
        aPage.replace(QLatin1String("%COPYRIGHT-INCLUDE%"), QzTools::readAllFileContents(QSL(":html/copyright")).toHtmlEscaped());

        aPage.replace(QLatin1String("%TITLE%"), tr("About Falkon"));
        aPage.replace(QLatin1String("%ABOUT-FALKON%"), tr("About Falkon"));
        aPage.replace(QLatin1String("%INFORMATIONS-ABOUT-VERSION%"), tr("Information about version"));
        aPage.replace(QLatin1String("%COPYRIGHT%"), tr("Copyright"));

        aPage.replace(QLatin1String("%VERSION-INFO%"),
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Version"),
#ifdef FALKON_GIT_REVISION
                              QSL("%1 (%2)").arg(QString::fromLatin1(Qz::VERSION), QL1S(FALKON_GIT_REVISION))));
#else
                              QString::fromLatin1(Qz::VERSION)));
#endif

        aPage.replace(QLatin1String("%MAIN-DEVELOPER%"), tr("Main developer"));

        QString developers = QSL("");
        for (const auto &author : Qz::AUTHORS) {
            developers += authorString(author);
        }

        aPage.replace(QLatin1String("%MAIN-DEVELOPER-TEXT%"), developers);
        aPage = QzTools::applyDirectionToPage(aPage);
    }

    return aPage;
}

QString FalkonSchemeReply::configPage()
{
    static QString cPage;

    if (cPage.isEmpty()) {
        cPage.append(QzTools::readAllFileContents(QSL(":html/config.html")));
        cPage.replace(QLatin1String("%ABOUT-IMG%"), QSL("qrc:icons/other/about.svg"));
        cPage.replace(QLatin1String("%ABOUT-IMG-DARK%"), QSL("qrc:icons/other/about-dark.svg"));

        cPage.replace(QLatin1String("%TITLE%"), tr("Configuration Information"));
        cPage.replace(QLatin1String("%CONFIG%"), tr("Configuration Information"));
        cPage.replace(QLatin1String("%INFORMATIONS-ABOUT-VERSION%"), tr("Information about version"));
        cPage.replace(QLatin1String("%CONFIG-ABOUT%"), tr("This page contains information about Falkon's current configuration - relevant for troubleshooting. Please include this information when submitting bug reports."));
        cPage.replace(QLatin1String("%BROWSER-IDENTIFICATION%"), tr("Browser Identification"));
        cPage.replace(QLatin1String("%PATHS%"), tr("Paths"));
        cPage.replace(QLatin1String("%BUILD-CONFIG%"), tr("Build Configuration"));
        cPage.replace(QLatin1String("%PREFS%"), tr("Preferences"));
        cPage.replace(QLatin1String("%OPTION%"), tr("Option"));
        cPage.replace(QLatin1String("%VALUE%"), tr("Value"));
        cPage.replace(QLatin1String("%PLUGINS%"), tr("Extensions"));
        cPage.replace(QLatin1String("%PL-NAME%"), tr("Name"));
        cPage.replace(QLatin1String("%PL-VER%"), tr("Version"));
        cPage.replace(QLatin1String("%PL-AUTH%"), tr("Author"));
        cPage.replace(QLatin1String("%PL-DESC%"), tr("Description"));

        auto allPaths = [](DataPaths::Path type) {
            QString out;
            const auto paths = DataPaths::allPaths(type);
            for (const QString &path : paths) {
                if (!out.isEmpty()) {
                    out.append(QSL("<br>"));
                }
                out.append(path);
            }
            return out;
        };

        cPage.replace(QLatin1String("%VERSION-INFO%"),
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Application version"),
#ifdef FALKON_GIT_REVISION
                              QSL("%1 (%2)").arg(QString::fromLatin1(Qz::VERSION), QL1S(FALKON_GIT_REVISION))
#else
                              QString::fromLatin1(Qz::VERSION)
#endif
                                                          ) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Qt version"), QString::fromLatin1(qVersion())) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("QtWebEngine version"), QSL(QTWEBENGINECORE_VERSION_STR)) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Platform"), QzTools::operatingSystemLong()));

        cPage.replace(QLatin1String("%PATHS-TEXT%"),
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Profile"), DataPaths::currentProfilePath()) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Settings"), DataPaths::currentProfilePath() + QSL("/settings.ini")) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Data"), allPaths(DataPaths::AppData)) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Themes"), allPaths(DataPaths::Themes)) +
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Extensions"), allPaths(DataPaths::Plugins)));

#ifdef QT_DEBUG
        QString debugBuild = tr("<b>Enabled</b>");
#else
        QString debugBuild = tr("Disabled");
#endif

#ifdef Q_OS_WIN
#if defined(Q_OS_WIN) && defined(W7API)
        QString w7APIEnabled = tr("<b>Enabled</b>");
#else
        QString w7APIEnabled = tr("Disabled");
#endif
#endif

        QString portableBuild = mApp->isPortable() ? tr("<b>Enabled</b>") : tr("Disabled");

        cPage.replace(QLatin1String("%BUILD-CONFIG-TEXT%"),
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Debug build"), debugBuild) +
#ifdef Q_OS_WIN
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Windows 7 API"), w7APIEnabled) +
#endif
                      QSL("<dt>%1</dt><dd>%2<dd>").arg(tr("Portable build"), portableBuild));

        cPage = QzTools::applyDirectionToPage(cPage);
    }

    QString page = cPage;
    page.replace(QLatin1String("%USER-AGENT%"), mApp->webProfile()->httpUserAgent());

    // Headless build: extension listing is a UI concept.
    QString pluginsString = QSL("<tr><td colspan=4 class=\"no-available-plugins\">%1</td></tr>").arg(tr("No available extensions."));

    page.replace(QLatin1String("%PLUGINS-INFO%"), pluginsString);

    QString allGroupsString;
    QSettings* settings = Settings::globalSettings();
    const auto groups = settings->childGroups();
    for (const QString &group : groups) {
        QString groupString = QSL("<tr><th colspan=\"2\">[%1]</th></tr>").arg(group);
        settings->beginGroup(group);

        const auto keys = settings->childKeys();
        for (const QString &key : keys) {
            const QVariant keyValue = settings->value(key);
            QString keyString;

            switch (keyValue.typeId()) {
            case QMetaType::QByteArray:
                keyString = QLatin1String("QByteArray");
                break;

            case QMetaType::QPoint: {
                const QPoint point = keyValue.toPoint();
                keyString = QSL("QPoint(%1, %2)").arg(point.x()).arg(point.y());
                break;
            }

            case QMetaType::QStringList:
                keyString = keyValue.toStringList().join(QSL(","));
                break;

            default:
                keyString = keyValue.toString();
            }

            if (keyString.isEmpty()) {
                keyString = QLatin1String("\"empty\"");
            }

            groupString.append(QSL("<tr><td>%1</td><td>%2</td></tr>").arg(key, keyString.toHtmlEscaped()));
        }

        settings->endGroup();
        allGroupsString.append(groupString);
    }

    page.replace(QLatin1String("%PREFS-INFO%"), allGroupsString);

    return page;
}
