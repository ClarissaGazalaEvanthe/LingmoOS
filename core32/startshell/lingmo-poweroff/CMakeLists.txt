set(plasma_shutdown_SRCS
    main.cpp
    shutdown.cpp
)

ecm_qt_declare_logging_category(plasma_shutdown_SRCS  HEADER debug.h IDENTIFIER PLASMA_SESSION CATEGORY_NAME org.kde.plasma.shutdown)

qt_add_dbus_adaptor(plasma_shutdown_SRCS org.kde.Shutdown.xml shutdown.h Shutdown)
qt_add_dbus_interface(plasma_shutdown_SRCS org.kde.Shutdown.xml shutdown_interface)
qt_add_dbus_interface( plasma_shutdown_SRCS ../../ksmserver/org.kde.KSMServerInterface.xml ksmserver_interface )
qt_add_dbus_interface( plasma_shutdown_SRCS ../../ksmserver/org.kde.KWin.Session.xml kwin_interface )

add_executable(lingmo-poweroff ${plasma_shutdown_SRCS})

target_link_libraries(lingmo-poweroff
    Qt::Core
    Qt::DBus
    KF5::ConfigCore
    PW::KWorkspace
)

kdbusaddons_generate_dbus_service_file(lingmo-poweroff org.kde.Shutdown ${KDE_INSTALL_FULL_BINDIR})
install(TARGETS lingmo-poweroff ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

