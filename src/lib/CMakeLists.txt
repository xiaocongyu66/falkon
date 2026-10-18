add_definitions(-DFALKON_SHAREDLIBRARY)

set(CMAKE_CXX_STANDARD 14) # Enable C++14, with cmake >= 3.1
set(CMAKE_CXX_EXTENSIONS OFF) # Don't enable gcc-specific extensions

set(SRCS
    3rdparty/qtsingleapplication/qtsingleapplication.cpp
    3rdparty/qtsingleapplication/qtlocalpeer.cpp
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include_directories(${CMAKE_SOURCE_DIR}/tests/modeltest)
endif()

include_directories(
    3rdparty
    adblock
    app
    autofill
    bookmarks
    cookies
    downloads
    history
    navigation
    network
    notifications
    opensearch
    other
    plugins
    popupwindow
    preferences
    session
    sidebar
    tabwidget
    tools
    webengine
    webtab
)

include_directories(${OPENSSL_INCLUDE_DIR})

set(SRCS ${SRCS}
    adblock/adblockurlinterceptor.cpp
    adblock/adblockmanager.cpp
    adblock/adblockmatcher.cpp
    adblock/adblocknetworkrequest.cpp
    adblock/adblockresourceschemehandler.cpp
    adblock/adblockrule.cpp
    adblock/adblockscripts.cpp
    adblock/adblocksearchtree.cpp
    adblock/adblocksubscription.cpp
    adblock/adblockplugin.cpp
    app/autosaver.cpp
    app/commandlineoptions.cpp
    app/datapaths.cpp
    app/mainapplication.cpp
    app/profilemanager.cpp
    app/qzcommon.cpp
    app/settings.cpp
    autofill/autofill.cpp
    autofill/passwordbackends/databaseencryptedpasswordbackend.cpp
    autofill/passwordbackends/databasepasswordbackend.cpp
    autofill/passwordbackends/passwordbackend.cpp
    autofill/passwordmanager.cpp
    bookmarks/bookmarkitem.cpp
    bookmarks/bookmarks.cpp
    bookmarks/bookmarksmodel.cpp
    cookies/cookiejar.cpp
    history/history.cpp
    history/historyitem.cpp
    history/historymodel.cpp
    network/networkmanager.cpp
    network/networkurlinterceptor.cpp
    network/schemehandlers/extensionschemehandler.cpp
    network/schemehandlers/falkonschemehandler.cpp
    opensearch/opensearchengine.cpp
    opensearch/opensearchenginedelegate.cpp
    opensearch/opensearchreader.cpp
    opensearch/searchenginesmanager.cpp
    other/qzsettings.cpp
    other/sitesettingsmanager.cpp
    other/useragentmanager.cpp
    other/protocolhandlermanager.cpp
    plugins/pluginproxy.cpp
    plugins/plugins.cpp
    plugins/ocssupport.cpp
    preferences/acceptlanguage.cpp
    tools/aesinterface.cpp
    tools/delayedfilewatcher.cpp
    tools/desktopfile.cpp
    tools/iconprovider.cpp
    tools/qztools.cpp
    tools/scripts.cpp
    tools/sqldatabase.cpp
    webengine/javascript/autofilljsobject.cpp
    webengine/javascript/externaljsobject.cpp
    webengine/loadrequest.cpp
    webengine/webpage.cpp

    adblock/adblockurlinterceptor.h
    adblock/adblockmanager.h
    adblock/adblockmatcher.h
    adblock/adblocknetworkrequest.h
    adblock/adblockresourceschemehandler.h
    adblock/adblockrule.h
    adblock/adblockscripts.h
    adblock/adblocksearchtree.h
    adblock/adblocksubscription.h
    adblock/adblockplugin.h
    app/autosaver.h
    app/commandlineoptions.h
    app/datapaths.h
    app/mainapplication.h
    app/profilemanager.h
    app/qzcommon.h
    app/settings.h
    autofill/autofill.h
    autofill/passwordbackends/databaseencryptedpasswordbackend.h
    autofill/passwordbackends/databasepasswordbackend.h
    autofill/passwordbackends/passwordbackend.h
    autofill/passwordmanager.h
    bookmarks/bookmarkitem.h
    bookmarks/bookmarks.h
    bookmarks/bookmarksmodel.h
    cookies/cookiejar.h
    history/history.h
    history/historyitem.h
    history/historymodel.h
    network/networkmanager.h
    network/networkurlinterceptor.h
    network/schemehandlers/extensionschemehandler.h
    network/schemehandlers/falkonschemehandler.h
    opensearch/opensearchengine.h
    opensearch/opensearchenginedelegate.h
    opensearch/opensearchreader.h
    opensearch/searchenginesmanager.h
    other/qzsettings.h
    other/sitesettingsmanager.h
    other/useragentmanager.h
    other/protocolhandlermanager.h
    plugins/pluginproxy.h
    plugins/plugins.h
    plugins/ocssupport.h
    preferences/acceptlanguage.h
    tools/aesinterface.h
    tools/delayedfilewatcher.h
    tools/desktopfile.h
    tools/iconprovider.h
    tools/qztools.h
    tools/scripts.h
    tools/sqldatabase.h
    webengine/javascript/autofilljsobject.h
    webengine/javascript/externaljsobject.h
    webengine/loadrequest.h
    webengine/webpage.h
)

# add the logging category for FALKON
ecm_qt_declare_logging_category(LOGGING_SRCS HEADER falkon_private_debug.h IDENTIFIER "FALKON_PRIVATE_LOG" CATEGORY_NAME "org.kde.falkon")

if (HAVE_LIBINTL)
endif()

if (WIN32)
endif()
if (APPLE)
    set(SRCS ${SRCS} tools/disablewindowtabbbing.mm)
endif()

# TODO: use ki18n_wrap_ui?
qt_wrap_ui(SRCS
)

qt_add_resources(SRCS
    data/data.qrc
    data/html.qrc
    data/icons.qrc
    data/breeze-fallback.qrc
    adblock/adblock.qrc
)

add_library(FalkonPrivate SHARED ${SRCS} ${LOGGING_SRCS})

get_property(QT_WEBENGINE_INCLUDE_DIRS TARGET Qt6::WebEngineCore PROPERTY INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(FalkonPrivate SYSTEM PUBLIC ${QT_WEBENGINE_INCLUDE_DIRS})

target_link_libraries(FalkonPrivate
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Network
    Qt6::NetworkPrivate
    Qt6::Sql
    Qt6::PrintSupport
    Qt6::QuickWidgets
    Qt6::WebChannel
    KF6::Archive
    ${OPENSSL_CRYPTO_LIBRARY}
)

if (BUILD_TESTING)
    target_link_libraries(FalkonPrivate Qt6::Test)
endif()

if (HAVE_LIBINTL)
    target_include_directories(FalkonPrivate SYSTEM PUBLIC ${Intl_INCLUDE_DIRS})
    target_link_libraries(FalkonPrivate ${Intl_LIBRARIES})
endif()

if (UNIX AND NOT APPLE)
    if (NOT NO_X11)
        target_link_libraries(FalkonPrivate XCB::XCB)
    endif()

    set_target_properties(FalkonPrivate PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION "3")
    install(TARGETS FalkonPrivate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

elseif (WIN32)

    set_target_properties(FalkonPrivate PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION "3")
    install(TARGETS FalkonPrivate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
endif()

if (APPLE)
    target_link_libraries(FalkonPrivate "-framework CoreServices -framework AppKit")
endif()

if (NOT DISABLE_DBUS)
    target_link_libraries(FalkonPrivate Qt::DBus)
endif()
