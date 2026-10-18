set( GreaseMonkey_SRCS
	gm_plugin.cpp
	gm_manager.cpp
	gm_script.cpp
	gm_downloader.cpp
	gm_jsobject.cpp
	)

ecm_create_qm_loader( GreaseMonkey_SRCS falkon_greasemonkey_qt )

set( GreaseMonkey_RSCS
	greasemonkey.qrc
	)
qt_add_resources(RSCS ${GreaseMonkey_RSCS})

add_library(GreaseMonkey MODULE ${GreaseMonkey_SRCS} ${RSCS})
install(TARGETS GreaseMonkey DESTINATION ${FALKON_INSTALL_PLUGINDIR})
target_link_libraries(GreaseMonkey FalkonPrivate)
