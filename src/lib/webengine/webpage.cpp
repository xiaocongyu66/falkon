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
#include "webpage.h"
#include "pluginproxy.h"
#include "mainapplication.h"
#include "qztools.h"
#include "autofill.h"
#include "iconprovider.h"
#include "qzsettings.h"
#include "useragentmanager.h"
#include "delayedfilewatcher.h"
#include "searchenginesmanager.h"
#include "sitesettingsmanager.h"
#include "javascript/externaljsobject.h"
#include "networkmanager.h"
#include "scripts.h"
#include "ocssupport.h"
#include "protocolhandlermanager.h"

#include <iostream>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWebChannel>
#include <QWebEngineHistory>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QTimer>
#include <QDesktopServices>
#include <QAuthenticator>
#include <QUrlQuery>
#include <QWebEngineProfile>
#include <QtWebEngineWidgetsVersion>
#include <QPointer>
#include <QEventLoop>

#include <QWebEngineRegisterProtocolHandlerRequest>
#include <QWebEngineNewWindowRequest>

QString WebPage::s_lastUploadLocation = QDir::homePath();
QUrl WebPage::s_lastUnsupportedUrl;
QElapsedTimer WebPage::s_lastUnsupportedUrlTime;
QStringList s_supportedSchemes;

static const bool kEnableJsOutput = qEnvironmentVariableIsSet("FALKON_ENABLE_JS_OUTPUT");

WebPage::WebPage(QObject* parent)
    : QWebEnginePage(mApp->webProfile(), parent)
    , m_fileWatcher(nullptr)
    , m_loadProgress(100)
    , m_secureStatus(false)
{
    /* Custom background color while the page is loading */
    setBackgroundColor(qzSettings->backgroundColorLoading);

    auto *channel = new QWebChannel(this);
    ExternalJsObject::setupWebChannel(channel, this);
    setWebChannel(channel, SafeJsWorld);

    connect(this, &QWebEnginePage::loadProgress, this, &WebPage::progress);
    connect(this, &QWebEnginePage::loadFinished, this, &WebPage::finished);
    connect(this, &QWebEnginePage::urlChanged, this, &WebPage::urlChanged);
    connect(this, &QWebEnginePage::featurePermissionRequested, this, &WebPage::featurePermissionRequested);
    connect(this, &QWebEnginePage::windowCloseRequested, this, &WebPage::windowCloseRequested);
    connect(this, &QWebEnginePage::fullScreenRequested, this, &WebPage::fullScreenRequested);
    connect(this, &QWebEnginePage::renderProcessTerminated, this, &WebPage::renderProcessTerminated);
    connect(this, &QWebEnginePage::certificateError, this, &WebPage::onCertificateError);

    connect(this, &QWebEnginePage::authenticationRequired, this, [this](const QUrl &url, QAuthenticator *auth) {
        mApp->networkManager()->authentication(url, auth);
    });

    connect(this, &QWebEnginePage::proxyAuthenticationRequired, this, [this](const QUrl &, QAuthenticator *auth, const QString &proxyHost) {
        mApp->networkManager()->proxyAuthentication(proxyHost, auth);
    });

    // Scroll to #fragment once the document layout is known
    m_contentsResizedConnection = connect(this, &QWebEnginePage::contentsSizeChanged, this, [this]() {
        const QString fragment = url().fragment();
        if (!fragment.isEmpty()) {
            runJavaScript(Scripts::scrollToAnchor(fragment));
        }
        disconnect(m_contentsResizedConnection);
    });

    connect(this, &QWebEnginePage::registerProtocolHandlerRequested, this, [this](QWebEngineRegisterProtocolHandlerRequest request) {
        delete m_registerProtocolHandlerRequest;
        m_registerProtocolHandlerRequest = new QWebEngineRegisterProtocolHandlerRequest(request);
    });
    connect(this, &QWebEnginePage::selectClientCertificate, this, [this](QWebEngineClientCertificateSelection selection) {
        // Headless: no prompt, just pick the first certificate.
        // Destroying the selection without select() rejects the request.
        const auto certs = selection.certificates();
        if (!certs.isEmpty()) {
            selection.select(certs.at(0));
        }
    });
    connect(this, &QWebEnginePage::newWindowRequested, this, &WebPage::onNewWindowRequested);
}

WebPage::~WebPage()
{
    delete m_registerProtocolHandlerRequest;
}

QWebEngineView *WebPage::view() const
{
    return QWebEngineView::forPage(this);
}

QVariant WebPage::execJavaScript(const QString &scriptSource, quint32 worldId, int timeout)
{
    QPointer<QEventLoop> loop = new QEventLoop;
    QVariant result;
    QTimer::singleShot(timeout, loop.data(), &QEventLoop::quit);

    runJavaScript(scriptSource, worldId, [loop, &result](const QVariant &res) {
        if (loop && loop->isRunning()) {
            result = res;
            loop->quit();
        }
    });

    loop->exec(QEventLoop::ExcludeUserInputEvents);
    delete loop;

    return result;
}

bool WebPage::isLoading() const
{
    return m_loadProgress < 100;
}

// static
QStringList WebPage::internalSchemes()
{
    return QStringList{
        QSL("http"),
        QSL("https"),
        QSL("file"),
        QSL("ftp"),
        QSL("data"),
        QSL("about"),
        QSL("view-source"),
        QSL("chrome"),
        QSL("abp-resource"),
        QSL("blob"),
    };
}

// static
QStringList WebPage::supportedSchemes()
{
    if (s_supportedSchemes.isEmpty()) {
        s_supportedSchemes = internalSchemes();
    }
    return s_supportedSchemes;
}

// static
void WebPage::addSupportedScheme(const QString &scheme)
{
    s_supportedSchemes = supportedSchemes();
    if (!s_supportedSchemes.contains(scheme)) {
        s_supportedSchemes.append(scheme);
    }
}

// static
void WebPage::removeSupportedScheme(const QString &scheme)
{
    s_supportedSchemes.removeOne(scheme);
}

void WebPage::urlChanged(const QUrl &url)
{
    Q_UNUSED(url)
}

void WebPage::progress(int prog)
{
    m_loadProgress = prog;
}

void WebPage::finished()
{
    progress(100);

    /* Custom background color when page is loaded */
    QTimer::singleShot(qzSettings->loadedSwitchDelay, this, [this](){
        setBackgroundColor(qzSettings->backgroundColorLoaded);
    });

    // File scheme watcher
    if (url().scheme() == QLatin1String("file")) {
        QFileInfo info(url().toLocalFile());
        if (info.isFile()) {
            if (!m_fileWatcher) {
                m_fileWatcher = new DelayedFileWatcher(this);
                connect(m_fileWatcher, &DelayedFileWatcher::delayedFileChanged, this, &WebPage::watchedFileChanged);
            }

            const QString filePath = url().toLocalFile();

            if (QFile::exists(filePath) && !m_fileWatcher->files().contains(filePath)) {
                m_fileWatcher->addPath(filePath);
            }
        }
    }
    else if (m_fileWatcher && !m_fileWatcher->files().isEmpty()) {
        m_fileWatcher->removePaths(m_fileWatcher->files());
    }

    // AutoFill
    m_autoFillUsernames = mApp->autoFill()->completePage(this, url());
}

void WebPage::watchedFileChanged(const QString &file)
{
    if (url().toLocalFile() == file) {
        triggerAction(QWebEnginePage::Reload);
    }
}

void WebPage::handleUnknownProtocol(const QUrl &url)
{
    const QString protocol = url.scheme();

    if (qzSettings->blockedProtocols.contains(protocol)) {
        qDebug() << "WebPage::handleUnknownProtocol Protocol" << protocol << "is blocked!";
        return;
    }

    if (qzSettings->autoOpenProtocols.contains(protocol)) {
        desktopServicesOpen(url);
        return;
    }

    // Headless: there is no user to ask about unknown protocols, ignore them.
    qWarning() << "WebPage: ignoring unknown protocol" << protocol << url;
}

void WebPage::desktopServicesOpen(const QUrl &url)
{
    // Open same url only once in 2 secs
    const int sameUrlTimeout = 2 * 1000;

    if ((s_lastUnsupportedUrl != url) || (!s_lastUnsupportedUrlTime.isValid()) || (s_lastUnsupportedUrlTime.elapsed() > sameUrlTimeout)) {
        s_lastUnsupportedUrl = url;
        s_lastUnsupportedUrlTime.restart();
        QDesktopServices::openUrl(url);
    }
    else {
        qWarning() << "WebPage::desktopServicesOpen Url" << url << "has already been opened!\n"
                   "Ignoring it to prevent infinite loop!";
    }
}

void WebPage::windowCloseRequested()
{
    // Headless: nothing to close, ignore the request
}

void WebPage::fullScreenRequested(QWebEngineFullScreenRequest fullScreenRequest)
{
    // Headless: never enter fullscreen
    fullScreenRequest.reject();
}

void WebPage::featurePermissionRequested(const QUrl &origin, const QWebEnginePage::Feature &feature)
{
    // Headless: no permission prompts, deny everything
    Q_UNUSED(origin)
    setFeaturePermission(origin, feature, PermissionDeniedByUser);
}

void WebPage::renderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus terminationStatus, int exitCode)
{
    Q_UNUSED(exitCode)

    if (terminationStatus == NormalTerminationStatus)
        return;

    qWarning() << "WebPage: render process terminated with status" << terminationStatus;
}

bool WebPage::acceptNavigationRequest(const QUrl &url, QWebEnginePage::NavigationType type, bool isMainFrame)
{
    if (mApp->isClosing()) {
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }

    if (!mApp->plugins()->acceptNavigationRequest(this, url, type, isMainFrame))
        return false;

    if (url.scheme() == QL1S("falkon")) {
        if (url.path() == QL1S("AddSearchProvider")) {
            QUrlQuery query(url);
            mApp->searchEnginesManager()->addEngine(QUrl(query.queryItemValue(QSL("url"))));
            return false;
        }
    }

    if (url.scheme() == QL1S("ocs") && OcsSupport::instance()->handleUrl(url)) {
        return false;
    }

    if (!internalSchemes().contains(url.scheme()) && !url.scheme().isEmpty()) {
        const auto *handler = profile()->urlSchemeHandler(url.scheme().toUtf8());

        if (handler == nullptr) {
            auto handlers = mApp->protocolHandlerManager()->protocolHandlers();

            if (!handlers.contains(url.scheme())) {
                handleUnknownProtocol(url);

                return false;
            }
        }
    }

    const bool result = QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);

    if (result) {
        if (isMainFrame) {
            const bool isWeb = url.scheme() == QL1S("http") || url.scheme() == QL1S("https") || url.scheme() == QL1S("file");

            if (isWeb) {
                auto webAttributes = mApp->siteSettingsManager()->getWebAttributes(url);
                if (!webAttributes.empty()) {
                    for (auto it = webAttributes.begin(); it != webAttributes.end(); ++it) {
                        settings()->setAttribute(it.key(), it.value());
                    }
                }
                else {
                    auto const webAttributes = mApp->siteSettingsManager()->getSupportedAttribute();
                    for (auto attribute : webAttributes) {
                        settings()->setAttribute(attribute, mApp->webSettings()->testAttribute(attribute));
                    }
                }
            }
            else {
                settings()->setAttribute(QWebEngineSettings::AutoLoadImages, true);
                settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
                settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
                settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
                settings()->setAttribute(QWebEngineSettings::JavascriptCanPaste, false);
                settings()->setAttribute(QWebEngineSettings::AllowWindowActivationFromJavaScript, false);
                settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
                settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, mApp->webSettings()->testAttribute(QWebEngineSettings::FullScreenSupportEnabled));
                settings()->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
                settings()->setAttribute(QWebEngineSettings::AllowGeolocationOnInsecureOrigins, false);
                settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, mApp->webSettings()->testAttribute(QWebEngineSettings::PlaybackRequiresUserGesture));
                settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, mApp->webSettings()->testAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly));
#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 6, 0)
                settings()->setAttribute(QWebEngineSettings::ReadingFromCanvasEnabled, mApp->webSettings()->testAttribute(QWebEngineSettings::ReadingFromCanvasEnabled));
#endif
#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 7, 0)
                settings()->setAttribute(QWebEngineSettings::ForceDarkMode, mApp->webSettings()->testAttribute(QWebEngineSettings::ForceDarkMode));
#endif
            }
        }
        Q_EMIT navigationRequestAccepted(url, type, isMainFrame);
    }

    return result;
}

void WebPage::onCertificateError(QWebEngineCertificateError error)
{
    auto mutableError = const_cast<QWebEngineCertificateError&>(error);
    if (mApp->networkManager()->certificateError(mutableError))
        mutableError.acceptCertificate();
    else
        mutableError.rejectCertificate();
}

void WebPage::onNewWindowRequested(QWebEngineNewWindowRequest &request)
{
    if (!mApp->plugins()->newWindowRequested(this, request)) {
        return;
    }

    // Headless: there are no tabs/windows to open the request in, deny it.
    qWarning() << "WebPage: denying new window request for" << request.requestedUrl();
}

QStringList WebPage::chooseFiles(QWebEnginePage::FileSelectionMode mode, const QStringList &oldFiles, const QStringList &acceptedMimeTypes)
{
    // Headless: no file dialogs, keep current selection untouched.
    Q_UNUSED(mode)
    Q_UNUSED(acceptedMimeTypes)
    return oldFiles;
}

QStringList WebPage::autoFillUsernames() const
{
    return m_autoFillUsernames;
}

QUrl WebPage::registerProtocolHandlerRequestUrl() const
{
    if (m_registerProtocolHandlerRequest && url().host() == m_registerProtocolHandlerRequest->origin().host()) {
        return m_registerProtocolHandlerRequest->origin();
    }
    return {};
}

QString WebPage::registerProtocolHandlerRequestScheme() const
{
    if (m_registerProtocolHandlerRequest && url().host() == m_registerProtocolHandlerRequest->origin().host()) {
        return m_registerProtocolHandlerRequest->scheme();
    }
    return {};
}

bool WebPage::javaScriptPrompt(const QUrl &securityOrigin, const QString &msg, const QString &defaultValue, QString* result)
{
    // Headless: no modal dialogs, cancel the prompt.
    Q_UNUSED(securityOrigin)
    Q_UNUSED(msg)
    Q_UNUSED(defaultValue)
    Q_UNUSED(result)
    return false;
}

bool WebPage::javaScriptConfirm(const QUrl &securityOrigin, const QString &msg)
{
    // Headless: no modal dialogs, cancel the confirmation.
    Q_UNUSED(securityOrigin)
    qWarning() << "WebPage: unhandled confirm():" << msg;
    return false;
}

void WebPage::javaScriptAlert(const QUrl &securityOrigin, const QString &msg)
{
    // Headless: no modal dialogs, ignore alerts.
    Q_UNUSED(securityOrigin)
    qWarning() << "WebPage: unhandled alert():" << msg;
}

void WebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID)
{
    if (!kEnableJsOutput) {
        return;
    }

    switch (level) {
    case InfoMessageLevel:
        std::cout << "[I] ";
        break;

    case WarningMessageLevel:
        std::cout << "[W] ";
        break;

    case ErrorMessageLevel:
        std::cout << "[E] ";
        break;
    }

    std::cout << qPrintable(sourceID) << ":" << lineNumber << " " << qPrintable(message) << '\n';
}
