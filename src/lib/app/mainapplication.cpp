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
#include "mainapplication.h"
#include "history.h"
#include "qztools.h"
#include "autofill.h"
#include "settings.h"
#include "datapaths.h"
#include "cookiejar.h"
#include "bookmarks.h"
#include "qzsettings.h"
#include "pluginproxy.h"
#include "iconprovider.h"
#include "networkmanager.h"
#include "profilemanager.h"
#include "useragentmanager.h"
#include "commandlineoptions.h"
#include "searchenginesmanager.h"
#include "sitesettingsmanager.h"
#include "scripts.h"
#include "protocolhandlermanager.h"
#include "../config.h"

#include <QWebEngineSettings>
#include <QSqlDatabase>
#include <QLibraryInfo>
#include <QTranslator>
#include <QThreadPool>
#include <QSettings>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>
#include <QRegularExpression>
#include <QtWebEngineWidgetsVersion>
#include <QtWebEngineCoreVersion>
#include <QtGuiVersion>
#include <QStyleHints>
#include <QNetworkInformation>

#include <QWebEngineUrlScheme>
#include <QWebEngineView>

#include <QCommandLineParser>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QPalette>
#include <functional>

#include "webpage.h"

#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <QWebEngineProfileBuilder>
#endif

#include <iostream>

#ifndef Q_OS_WIN
static void headlessMsgHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (msg.startsWith(QL1S("QSslSocket: cannot resolve SSL")))
        return;
    if (msg.startsWith(QL1S("Remote debugging server started successfully.")))
        return;

    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
    case QtWarningMsg:
    case QtCriticalMsg:
        std::cerr << qPrintable(qFormatLogMessage(type, context, msg)) << std::endl;
        break;

    case QtFatalMsg:
        std::cerr << "Fatal: " << qPrintable(qFormatLogMessage(type, context, msg)) << std::endl;
        abort();

    default:
        break;
    }
}
#endif

static bool s_testMode = false;

MainApplication::MainApplication(int &argc, char** argv)
    : QtSingleApplication(argc, argv)
    , m_isPrivate(false)
    , m_isPortable(false)
    , m_isClosing(false)
    , m_history(nullptr)
    , m_bookmarks(nullptr)
    , m_autoFill(nullptr)
    , m_cookieJar(nullptr)
    , m_plugins(nullptr)
    , m_networkManager(nullptr)
    , m_userAgentManager(nullptr)
    , m_searchEnginesManager(nullptr)
    , m_protocolHandlerManager(nullptr)
    , m_siteSettingsManager(nullptr)
    , m_webProfile(nullptr)
{
#ifndef Q_OS_WIN
    qInstallMessageHandler(&headlessMsgHandler);
#endif

    setApplicationName(QStringLiteral("falkon"));
    setOrganizationDomain(QStringLiteral("org.kde"));
    setApplicationVersion(QString::fromLatin1(Qz::VERSION));

    QList<QUrl> startUrls;
    QString startProfile;

    bool noAddons = false;
    bool newInstance = false;

    if (argc > 1) {
        CommandLineOptions cmd;
        const auto actions = cmd.getActions();
        for (const CommandLineOptions::ActionPair &pair : actions) {
            switch (pair.action) {
            case Qz::CL_StartWithoutAddons:
                noAddons = true;
                break;
            case Qz::CL_StartWithProfile:
                startProfile = pair.text;
                break;
            case Qz::CL_StartPortable:
                m_isPortable = true;
                break;
            case Qz::CL_StartPrivateBrowsing:
                m_isPrivate = true;
                break;
            case Qz::CL_StartNewInstance:
                newInstance = true;
                break;
            case Qz::CL_HeadlessRender:
                m_headlessRequested = true;
                m_headlessUrl = QUrl::fromUserInput(pair.text);
                break;
            case Qz::CL_ExitAction:
                m_isClosing = true;
                return;
            default:
                break;
            }
        }
    }

    if (!isPortable()) {
        QSettings falkonConf(QSL("%1/falkon.conf").arg(applicationDirPath()), QSettings::IniFormat);
        m_isPortable = falkonConf.value(QSL("Config/Portable")).toBool();
    }

    if (isPortable()) {
        std::cout << "Falkon: Running in Portable Mode." << std::endl;
        DataPaths::setPortableVersion();
    }

    // A second instance has nothing useful to do headless - exit quietly.
    if (isRunning() && !m_headlessRequested) {
        m_isClosing = true;
        return;
    }

    if (newInstance && (startProfile.isEmpty() || startProfile == QLatin1String("default"))) {
        std::cout << "New instance cannot be started with default profile!" << std::endl;
        newInstance = false;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);

    ProfileManager profileManager;
    profileManager.initConfigDir();
    profileManager.initCurrentProfile(startProfile);

    Settings::createSettings(DataPaths::currentProfilePath() + QLatin1String("/settings.ini"));

    setChromiumFlags();
    NetworkManager::registerSchemes();
    registerAllowedSchemes();

    if (!setupWebEngineProfile(startProfile)) {
        m_isClosing = true;
        return;
    }

    m_networkManager = new NetworkManager(this);

    initNetworkInformation();
    setupUserScripts();

    loadSettings();

    m_plugins = new PluginProxy(this);
    m_autoFill = new AutoFill(this);
    mApp->protocolHandlerManager();

    if (!noAddons)
        m_plugins->loadPlugins();

    if (m_headlessRequested) {
        QTimer::singleShot(0, this, &MainApplication::runHeadlessRender);
    } else {
        std::cerr << "Falkon: only --headless-render is supported in this build" << std::endl;
        m_isClosing = true;
        return;
    }

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, DataPaths::currentProfilePath());

    connect(this, SIGNAL(messageReceived(QString)), this, SLOT(messageReceived(QString)));
    connect(this, &QCoreApplication::aboutToQuit, this, &MainApplication::saveSettings);
}

MainApplication::~MainApplication()
{
    m_isClosing = true;

    IconProvider::instance()->saveIconsToDatabase();

    // Wait for all QtConcurrent jobs to finish
    QThreadPool::globalInstance()->waitForDone();

    // Delete all classes that are saving data in destructor
    delete m_bookmarks;
    m_bookmarks = nullptr;
    delete m_cookieJar;
    m_cookieJar = nullptr;

    // On Qt 6, deleting the web profile is necessary in order to make sure cache, cookies, etc. are flushed to disk.
    delete m_webProfile;
    m_webProfile = nullptr;

    Settings::syncSettings();
}

void MainApplication::setChromiumFlags()
{
    QByteArray chromium_flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    chromium_flags.append(" --enable-features=WebRTCPipeWireCapturer");

    if (Settings::globalSettings()->value("Web-Browser-Settings/hardwareAccel", false).toBool()) {
        chromium_flags.append(" --enable-oop-rasterization --enable-gpu-rasterization --enable-native-gpu-memory-buffers");
#if QTWEBENGINECORE_VERSION < QT_VERSION_CHECK(6, 9, 0)
        chromium_flags.append(" --use-gl=desktop");
#endif
    }

    int prefferColorScheme = Settings::globalSettings()->value("Web-Browser-Settings/prefferColorScheme", Qz::ColorScheme_Auto).toInt();
    if (   (prefferColorScheme == Qz::ColorScheme_Dark)
#if QTGUI_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        || ((prefferColorScheme == Qz::ColorScheme_Auto) && (styleHints()->colorScheme() == Qt::ColorScheme::Dark))
#endif
    ) {
        chromium_flags.append(" --blink-settings=preferredColorScheme=0");
    }
    else if ((prefferColorScheme == Qz::ColorScheme_Light)
#if QTGUI_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        || ((prefferColorScheme == Qz::ColorScheme_Auto) && (styleHints()->colorScheme() == Qt::ColorScheme::Light))
#endif
    ) {
        chromium_flags.append(" --blink-settings=preferredColorScheme=1");
    }

    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromium_flags);
}

bool MainApplication::isClosing() const
{
    return m_isClosing;
}

bool MainApplication::isPrivate() const
{
    return m_isPrivate;
}

bool MainApplication::isPortable() const
{
#ifdef PORTABLE_BUILD
    return true;
#else
    return m_isPortable;
#endif
}

void MainApplication::reloadSettings()
{
    loadSettings();
    Q_EMIT settingsReloaded();
}

History* MainApplication::history()
{
    if (!m_history) {
        m_history = new History(this);
    }
    return m_history;
}

Bookmarks* MainApplication::bookmarks()
{
    if (!m_bookmarks) {
        m_bookmarks = new Bookmarks(this);
    }
    return m_bookmarks;
}

AutoFill* MainApplication::autoFill()
{
    return m_autoFill;
}

CookieJar* MainApplication::cookieJar()
{
    if (!m_cookieJar) {
        m_cookieJar = new CookieJar(this);
    }
    return m_cookieJar;
}

PluginProxy* MainApplication::plugins()
{
    return m_plugins;
}

NetworkManager *MainApplication::networkManager()
{
    return m_networkManager;
}

UserAgentManager* MainApplication::userAgentManager()
{
    if (!m_userAgentManager) {
        m_userAgentManager = new UserAgentManager(this);
    }
    return m_userAgentManager;
}

SearchEnginesManager* MainApplication::searchEnginesManager()
{
    if (!m_searchEnginesManager) {
        m_searchEnginesManager = new SearchEnginesManager(this);
    }
    return m_searchEnginesManager;
}

ProtocolHandlerManager *MainApplication::protocolHandlerManager()
{
    if (!m_protocolHandlerManager) {
        m_protocolHandlerManager = new ProtocolHandlerManager(this);
    }
    return m_protocolHandlerManager;
}

SiteSettingsManager * MainApplication::siteSettingsManager()
{
    if (!m_siteSettingsManager) {
        m_siteSettingsManager = new SiteSettingsManager(this);
    }
    return m_siteSettingsManager;
}

QWebEngineProfile *MainApplication::webProfile() const
{
    return m_webProfile;
}

QWebEngineSettings *MainApplication::webSettings() const
{
    return m_webProfile->settings();
}

// static
MainApplication* MainApplication::instance()
{
    return static_cast<MainApplication*>(QCoreApplication::instance());
}

// static
bool MainApplication::isTestModeEnabled()
{
    return s_testMode;
}

// static
void MainApplication::setTestModeEnabled(bool enabled)
{
    s_testMode = enabled;
}

bool MainApplication::setupWebEngineProfile(const QString &startProfile)
{
    // QSQLITE database plugin is required (cookies/icons backends)
    if (!QSqlDatabase::isDriverAvailable(QSL("QSQLITE"))) {
        qCritical() << "Qt SQLite database plugin is not available. Please install it and restart the application.";
        return false;
    }

    if (isPrivate()) {
#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        m_webProfile = QWebEngineProfileBuilder::createOffTheRecordProfile();
#else
        m_webProfile = new QWebEngineProfile();
#endif
        return true;
    }

#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    Settings settings;
    const bool allowCache = settings.value(QSL("Web-Browser-Settings/AllowLocalCache"), true).toBool();
    const int cacheSize = settings.value(QSL("Web-Browser-Settings/LocalCacheSize"), 50).toInt() * 1000 * 1000;

    QString defaultCachePath = DataPaths::path(DataPaths::Cache);
    if (!defaultCachePath.startsWith(DataPaths::currentProfilePath())) {
        defaultCachePath.append(QLatin1Char('/') + ProfileManager::currentProfile());
    }
    const QString cachePath = settings.value(QSL("Web-Browser-Settings/CachePath"), defaultCachePath).toString();

    QWebEngineProfileBuilder profileBuilder;
    profileBuilder.setPersistentStoragePath(DataPaths::currentProfilePath());
    profileBuilder.setCachePath(cachePath);
    profileBuilder.setHttpCacheType(allowCache ? QWebEngineProfile::DiskHttpCache : QWebEngineProfile::MemoryHttpCache);
    profileBuilder.setHttpCacheMaximumSize(cacheSize);
    profileBuilder.setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profileBuilder.setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);

    QString storageName = startProfile.isEmpty() ? QSL("default") : startProfile;
    m_webProfile = profileBuilder.createProfile(storageName);

    if (!m_webProfile) {
        qCritical() << "Failed to create WebEngine profile! Storage path may be in use.";
        qCritical() << "Profile path:" << DataPaths::currentProfilePath();
        m_webProfile = QWebEngineProfileBuilder::createOffTheRecordProfile();
    }
#else
    m_webProfile = new QWebEngineProfile(startProfile.isEmpty() ? QSL("default") : startProfile);
#endif

    return m_webProfile != nullptr;
}

void MainApplication::messageReceived(const QString &message)
{
    // Headless: instances cannot open windows/tabs. A second startup with
    // --headless-render never reaches here (it runs its own render), any
    // other message is ignored.
    std::cout << "Falkon: ignoring message from other instance: " << qPrintable(message) << std::endl;
}

void MainApplication::saveSettings()
{
    if (isPrivate() || m_headlessRequested) {
        return;
    }

    m_isClosing = true;

    m_plugins->shutdown();
    m_networkManager->shutdown();

    qzSettings->saveSettings();
}

void MainApplication::loadSettings()
{
    QWebEngineSettings* webSettings = m_webProfile->settings();

    // Web browsing settings
    Settings settings;
    settings.beginGroup(QSL("Web-Browser-Settings"));

    webSettings->setAttribute(QWebEngineSettings::LocalStorageEnabled, settings.value(QSL("HTML5StorageEnabled"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::PluginsEnabled, settings.value(QSL("allowPlugins"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::JavascriptEnabled, settings.value(QSL("allowJavaScript"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, settings.value(QSL("allowJavaScriptOpenWindow"), false).toBool());
    webSettings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, settings.value(QSL("allowJavaScriptAccessClipboard"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::LinksIncludedInFocusChain, settings.value(QSL("IncludeLinkInFocusChain"), false).toBool());
    webSettings->setAttribute(QWebEngineSettings::PrintElementBackgrounds, settings.value(QSL("PrintElementBackground"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, settings.value(QSL("SpatialNavigation"), false).toBool());
    webSettings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, settings.value(QSL("AnimateScrolling"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    webSettings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    webSettings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    webSettings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    webSettings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);

    webSettings->setAttribute(QWebEngineSettings::AllowWindowActivationFromJavaScript, settings.value(QSL("allowJavaScriptActivateWindow"), false).toBool());

    webSettings->setAttribute(QWebEngineSettings::JavascriptCanPaste, settings.value(QSL("allowJavaScriptPaste"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, settings.value(QSL("DisableVideoAutoPlay"), false).toBool());
    webSettings->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, settings.value(QSL("WebRTCPublicIpOnly"), true).toBool());
    webSettings->setUnknownUrlSchemePolicy(QWebEngineSettings::AllowAllUnknownUrlSchemes);
    webSettings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, settings.value(QSL("DNSPrefetch"), true).toBool());
    webSettings->setAttribute(QWebEngineSettings::PdfViewerEnabled, settings.value(QSL("intPDFViewer"), false).toBool());
    webSettings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, settings.value(QSL("screenCaptureEnabled"), true).toBool());

#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    webSettings->setAttribute(QWebEngineSettings::ReadingFromCanvasEnabled, settings.value(QSL("readingFromCanvasEnabled"), true).toBool());
#endif
#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    webSettings->setAttribute(QWebEngineSettings::ForceDarkMode, settings.value(QSL("forceDarkMode"), false).toBool());
#endif
#if QTWEBENGINECORE_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const bool allowImageAnimation = settings.value(QSL("allowImageAnimation"), true).toBool();
    if (allowImageAnimation) {
        webSettings->setImageAnimationPolicy(QWebEngineSettings::ImageAnimationPolicy::Allow);
    }
    else {
        webSettings->setImageAnimationPolicy(QWebEngineSettings::ImageAnimationPolicy::Disallow);
    }
#endif

    webSettings->setDefaultTextEncoding(settings.value(QSL("DefaultEncoding"), webSettings->defaultTextEncoding()).toString());

    const QString userCss = settings.value(QSL("userStyleSheet"), QString()).toString();
    settings.endGroup();

    setUserStyleSheet(userCss);

    settings.beginGroup(QSL("Browser-Fonts"));
    webSettings->setFontFamily(QWebEngineSettings::StandardFont, settings.value(QSL("StandardFont"), webSettings->fontFamily(QWebEngineSettings::StandardFont)).toString());
    webSettings->setFontFamily(QWebEngineSettings::CursiveFont, settings.value(QSL("CursiveFont"), webSettings->fontFamily(QWebEngineSettings::CursiveFont)).toString());
    webSettings->setFontFamily(QWebEngineSettings::FantasyFont, settings.value(QSL("FantasyFont"), webSettings->fontFamily(QWebEngineSettings::FantasyFont)).toString());
    webSettings->setFontFamily(QWebEngineSettings::FixedFont, settings.value(QSL("FixedFont"), webSettings->fontFamily(QWebEngineSettings::FixedFont)).toString());
    webSettings->setFontFamily(QWebEngineSettings::SansSerifFont, settings.value(QSL("SansSerifFont"), webSettings->fontFamily(QWebEngineSettings::SansSerifFont)).toString());
    webSettings->setFontFamily(QWebEngineSettings::SerifFont, settings.value(QSL("SerifFont"), webSettings->fontFamily(QWebEngineSettings::SerifFont)).toString());
    webSettings->setFontSize(QWebEngineSettings::DefaultFontSize, settings.value(QSL("DefaultFontSize"), 15).toInt());
    webSettings->setFontSize(QWebEngineSettings::DefaultFixedFontSize, settings.value(QSL("FixedFontSize"), 14).toInt());
    webSettings->setFontSize(QWebEngineSettings::MinimumFontSize, settings.value(QSL("MinimumFontSize"), 3).toInt());
    webSettings->setFontSize(QWebEngineSettings::MinimumLogicalFontSize, settings.value(QSL("MinimumLogicalFontSize"), 5).toInt());
    settings.endGroup();

    QWebEngineProfile* profile = m_webProfile;
#if QTWEBENGINECORE_VERSION < QT_VERSION_CHECK(6, 9, 0)
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profile->setPersistentStoragePath(DataPaths::currentProfilePath());

    QString defaultPath = DataPaths::path(DataPaths::Cache);
    if (!defaultPath.startsWith(DataPaths::currentProfilePath()))
        defaultPath.append(QLatin1Char('/') + ProfileManager::currentProfile());
    const QString &cachePath = settings.value(QSL("Web-Browser-Settings/CachePath"), defaultPath).toString();
    profile->setCachePath(cachePath);

    const bool allowCache = settings.value(QSL("Web-Browser-Settings/AllowLocalCache"), true).toBool();
    profile->setHttpCacheType(allowCache ? QWebEngineProfile::DiskHttpCache : QWebEngineProfile::MemoryHttpCache);

    const int cacheSize = settings.value(QSL("Web-Browser-Settings/LocalCacheSize"), 50).toInt() * 1000 * 1000;
    profile->setHttpCacheMaximumSize(cacheSize);
#endif

    settings.beginGroup(QSL("SpellCheck"));
    profile->setSpellCheckEnabled(settings.value(QSL("Enabled"), false).toBool());
    profile->setSpellCheckLanguages(settings.value(QSL("Languages")).toStringList());
    settings.endGroup();

    if (isPrivate()) {
        history()->setSaving(false);
    }

    qzSettings->loadSettings();
    userAgentManager()->loadSettings();
    networkManager()->loadSettings();
}

void MainApplication::registerAllowedSchemes()
{
    for (const QString &schemeName : std::as_const(qzSettings->allowedSchemes)) {
        if (qzSettings->blockedSchemes.contains(schemeName)) {
            continue;
        }
        QWebEngineUrlScheme scheme(schemeName.toUtf8());
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
        scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
        QWebEngineUrlScheme::registerScheme(scheme);
    }
}

void MainApplication::initNetworkInformation()
{
    bool loaded = QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability |
                                                             QNetworkInformation::Feature::CaptivePortal);
    if (!loaded) {
        QNetworkInformation::loadDefaultBackend();
    }
}

void MainApplication::setupUserScripts()
{
    // WebChannel for SafeJsWorld
    QWebEngineScript script;
    script.setName(QSL("_falkon_webchannel"));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(WebPage::SafeJsWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(Scripts::setupWebChannel());
    m_webProfile->scripts()->insert(script);

    // document.window object addons
    QWebEngineScript documentWindowAddons;
    documentWindowAddons.setName(QSL("_falkon_window_object"));
    documentWindowAddons.setInjectionPoint(QWebEngineScript::DocumentCreation);
    documentWindowAddons.setWorldId(WebPage::UnsafeJsWorld);
    documentWindowAddons.setRunsOnSubFrames(true);
    documentWindowAddons.setSourceCode(Scripts::setupWindowObject());
    m_webProfile->scripts()->insert(documentWindowAddons);
}

void MainApplication::setUserStyleSheet(const QString &filePath)
{
    QString userCss;

#if !defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // Don't grey out selection on losing focus (to prevent graying out found text)
    QString highlightColor;
    QString highlightedTextColor;
#ifdef Q_OS_MACOS
    highlightColor = QLatin1String("#b6d6fc");
    highlightedTextColor = QLatin1String("#000");
#else
    QPalette pal = style()->standardPalette();
    highlightColor = pal.color(QPalette::Highlight).name();
    highlightedTextColor = pal.color(QPalette::HighlightedText).name();
#endif
    userCss += QStringLiteral("::selection {background: %1; color: %2;} ").arg(highlightColor, highlightedTextColor);
#endif

    userCss += QzTools::readAllFileContents(filePath).remove(QLatin1Char('\n'));

    const QString name = QStringLiteral("_falkon_userstylesheet");
    for (const QWebEngineScript &oldScript : m_webProfile->scripts()->find(name)) {
        m_webProfile->scripts()->remove(oldScript);
    }

    if (userCss.isEmpty())
        return;

    QWebEngineScript script;
    script.setName(name);
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(WebPage::SafeJsWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(Scripts::setCss(userCss));
    m_webProfile->scripts()->insert(script);
}

void MainApplication::runHeadlessRender()
{
    // Re-parse local headless parameters (main parser belongs to CommandLineOptions)
    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    const QCommandLineOption outOpt(QSL("headless-output"), QSL("Output file"), QSL("file"), QSL("out.png"));
    const QCommandLineOption wOpt(QSL("headless-width"), QSL("Viewport width"), QSL("px"), QSL("800"));
    const QCommandLineOption hOpt(QSL("headless-height"), QSL("Viewport height"), QSL("px"), QSL("600"));
    const QCommandLineOption tOpt(QSL("headless-timeout"), QSL("Timeout (ms)"), QSL("ms"), QSL("30000"));
    parser.addOption(outOpt);
    parser.addOption(wOpt);
    parser.addOption(hOpt);
    parser.addOption(tOpt);
    parser.parse(QCoreApplication::arguments());

    const QString outputPath = parser.value(outOpt);
    const int width = qMax(1, parser.value(wOpt).toInt());
    const int height = qMax(1, parser.value(hOpt).toInt());
    const int timeout = qMax(1000, parser.value(tOpt).toInt());

    auto *view = new QWebEngineView();
    // Falkon WebPage: adblock interception, user scripts, certificate handling
    auto *page = new WebPage(view);
    view->setPage(page);
    view->resize(width, height);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool done = false;

    std::function<void(int)> finish = [&](int code) {
        if (done) {
            return;
        }
        done = true;
        loop.exit(code);
    };

    QObject::connect(&timeoutTimer, &QTimer::timeout, view, [&finish, timeout]() {
        std::cout << "Falkon headless: render timed out after " << timeout << " ms" << std::endl;
        finish(2);
    });

    // Readiness: main document complete + no pending images + fonts.ready
    const QString checkScript = QSL(
        "(function(){"
        "  if (document.readyState !== 'complete') return 'wait';"
        "  for (var i = 0; i < document.images.length; ++i) { if (!document.images[i].complete) return 'wait'; }"
        "  return document.fonts && document.fonts.status === 'loading' ? 'wait' : 'ready';"
        "})()");

    QTimer pollTimer;
    pollTimer.setInterval(100);
    QObject::connect(&pollTimer, &QTimer::timeout, view, [&]() {
        view->page()->runJavaScript(checkScript, 0, [&](const QVariant &result) {
            if (done || result.toString() != QSL("ready")) {
                return;
            }
            pollTimer.stop();
            // Wait two frames so the compositor flushes
            view->page()->runJavaScript(QSL(
                "(function(){return new Promise(function(res){"
                "  requestAnimationFrame(function(){requestAnimationFrame(function(){res(1);});});"
                "});})()"), 0, [&](const QVariant &) {
                const QPixmap shot = view->grab();
                if (shot.save(outputPath)) {
                    std::cout << "Falkon headless: saved " << qPrintable(outputPath) << std::endl;
                    finish(0);
                } else {
                    std::cout << "Falkon headless: failed to save " << qPrintable(outputPath) << std::endl;
                    finish(3);
                }
            });
        });
    });

    QObject::connect(view, &QWebEngineView::loadFinished, view, [&](bool ok) {
        if (!ok) {
            std::cout << "Falkon headless: page failed to load" << std::endl;
            finish(4);
        }
    });

    view->show();
    view->load(m_headlessUrl);
    pollTimer.start();
    timeoutTimer.start(timeout);
    const int code = loop.exec();
    timeoutTimer.stop();
    pollTimer.stop();
    view->deleteLater();
    QCoreApplication::exit(code);
}
