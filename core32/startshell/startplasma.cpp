/*
    SPDX-FileCopyrightText: 2019 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include <config-startplasma.h>

#include <QDir>
#include <QEventLoop>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>

#include <KConfig>
#include <KConfigGroup>
#include <KNotifyConfig>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KSharedConfig>

#include <phonon/audiooutput.h>
#include <phonon/mediaobject.h>
#include <phonon/mediasource.h>

#include <unistd.h>

#include <autostartscriptdesktopfile.h>
#include <updatelaunchenvjob.h>

#include "startplasma.h"

#include "../config-workspace.h"
#include "../kcms/lookandfeel/lookandfeelmanager.h"
#include "debug.h"

QTextStream out(stderr);

void sigtermHandler(int signalNumber)
{
    Q_UNUSED(signalNumber)
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->exit(-1);
    }
}

void messageBox(const QString &text)
{
    out << text;
    runSync(QStringLiteral("xmessage"), {QStringLiteral("-geometry"), QStringLiteral("500x100"), text});
}

QStringList allServices(const QLatin1String &prefix)
{
    const QStringList services = QDBusConnection::sessionBus().interface()->registeredServiceNames();
    QStringList names;

    std::copy_if(services.cbegin(), services.cend(), std::back_inserter(names), [&prefix](const QString &serviceName) {
        return serviceName.startsWith(prefix);
    });

    return names;
}

void gentleTermination(QProcess *p)
{
    if (p->state() != QProcess::Running) {
        return;
    }

    p->terminate();

    // Wait longer for a session than a greeter
    if (!p->waitForFinished(5000)) {
        p->kill();
        if (!p->waitForFinished(5000)) {
            qCWarning(PLASMA_STARTUP) << "Could not fully finish the process" << p->program();
        }
    }
}

int runSync(const QString &program, const QStringList &args, const QStringList &env)
{
    QProcess p;
    if (!env.isEmpty())
        p.setEnvironment(QProcess::systemEnvironment() << env);
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start(program, args);

    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &p, [&p] {
        gentleTermination(&p);
    });
    //     qCDebug(PLASMA_STARTUP) << "started..." << program << args;
    p.waitForFinished(-1);
    if (p.exitCode()) {
        qCWarning(PLASMA_STARTUP) << program << args << "exited with code" << p.exitCode();
    }
    return p.exitCode();
}

bool isShellVariable(const QByteArray &name)
{
    return name == "_" || name == "SHELL" || name.startsWith("SHLVL");
}

bool isSessionVariable(const QByteArray &name)
{
    // Check is variable is specific to session.
    return name == "DISPLAY" || name == "XAUTHORITY" || //
        name == "WAYLAND_DISPLAY" || name == "WAYLAND_SOCKET" || //
        name.startsWith("XDG_");
}

void setEnvironmentVariable(const QByteArray &name, const QByteArray &value)
{
    if (qgetenv(name) != value) {
        qputenv(name, value);
    }
}

void sourceFiles(const QStringList &files)
{
    QStringList filteredFiles;
    std::copy_if(files.begin(), files.end(), std::back_inserter(filteredFiles), [](const QString &i) {
        return QFileInfo(i).isReadable();
    });

    if (filteredFiles.isEmpty())
        return;

    filteredFiles.prepend(QStringLiteral(CMAKE_INSTALL_FULL_LIBEXECDIR "/lingmo-sourceenv.sh"));

    QProcess p;
    p.start(QStringLiteral("/bin/sh"), filteredFiles);
    p.waitForFinished(-1);

    const auto fullEnv = p.readAllStandardOutput();
    auto envs = fullEnv.split('\0');

    for (auto &env : envs) {
        const int idx = env.indexOf('=');
        if (Q_UNLIKELY(idx <= 0)) {
            continue;
        }

        const auto name = env.left(idx);
        if (isShellVariable(name)) {
            continue;
        }
        setEnvironmentVariable(name, env.mid(idx + 1));
    }
}

void createConfigDirectory()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!QDir().mkpath(configDir))
        out << "Could not create config directory XDG_CONFIG_HOME: " << configDir << '\n';
}

void runStartupConfig()
{
    // export LC_* variables set by kcmshell5 formats into environment
    // so it can be picked up by QLocale and friends.
    KConfig config(QStringLiteral("lingmo-localerc"));
    KConfigGroup formatsConfig = KConfigGroup(&config, "Formats");

    const auto lcValues = {"LANG", "LC_NUMERIC", "LC_TIME", "LC_MONETARY", "LC_MEASUREMENT", "LC_COLLATE", "LC_CTYPE"};
    for (auto lc : lcValues) {
        const QString value = formatsConfig.readEntry(lc, QString());
        if (!value.isEmpty()) {
            qputenv(lc, value.toUtf8());
        }
    }

    KConfigGroup languageConfig = KConfigGroup(&config, "Translations");
    const QString value = languageConfig.readEntry("LANGUAGE", QString());
    if (!value.isEmpty()) {
        qputenv("LANGUAGE", value.toUtf8());
    }

    if (!formatsConfig.hasKey("LANG") && !qEnvironmentVariableIsEmpty("LANG")) {
        formatsConfig.writeEntry("LANG", qgetenv("LANG"));
        formatsConfig.sync();
    }
}

void setupCursor(bool wayland)
{
    const KConfig cfg(QStringLiteral("kcminputrc"));
    const KConfigGroup inputCfg = cfg.group("Mouse");

    const auto kcminputrc_mouse_cursorsize = inputCfg.readEntry("cursorSize", 24);
    const auto kcminputrc_mouse_cursortheme = inputCfg.readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
    if (!kcminputrc_mouse_cursortheme.isEmpty()) {
#ifdef XCURSOR_PATH
        QByteArray path(XCURSOR_PATH);
        path.replace("$XCURSOR_PATH", qgetenv("XCURSOR_PATH"));
        qputenv("XCURSOR_PATH", path);
#endif
    }

    // TODO: consider linking directly
    const int applyMouseStatus =
        wayland ? 0 : runSync(QStringLiteral("kapplymousetheme"), {kcminputrc_mouse_cursortheme, QString::number(kcminputrc_mouse_cursorsize)});
    if (applyMouseStatus == 10) {
        qputenv("XCURSOR_THEME", "breeze_cursors");
    } else if (!kcminputrc_mouse_cursortheme.isEmpty()) {
        qputenv("XCURSOR_THEME", kcminputrc_mouse_cursortheme.toUtf8());
    }
    qputenv("XCURSOR_SIZE", QByteArray::number(kcminputrc_mouse_cursorsize));
}

std::optional<QProcessEnvironment> getSystemdEnvironment()
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                              QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Manager") << QStringLiteral("Environment");
    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return std::nullopt;
    }

    // Make sure the returned type is correct.
    auto arguments = reply.arguments();
    if (arguments.isEmpty() || arguments[0].userType() != qMetaTypeId<QDBusVariant>()) {
        return std::nullopt;
    }
    auto variant = qdbus_cast<QVariant>(arguments[0]);
    if (variant.type() != QVariant::StringList) {
        return std::nullopt;
    }

    const auto assignmentList = variant.toStringList();
    QProcessEnvironment ret;
    for (auto &env : assignmentList) {
        const int idx = env.indexOf(QLatin1Char('='));
        if (Q_LIKELY(idx > 0)) {
            ret.insert(env.left(idx), env.mid(idx + 1));
        }
    }

    return ret;
}

// Import systemd user environment.
//
// Systemd read ~/.config/environment.d which applies to all systemd user unit.
// But it won't work if plasma is not started by systemd.
void importSystemdEnvrionment()
{
    const auto environment = getSystemdEnvironment();
    if (!environment) {
        return;
    }

    for (auto &nameStr : environment.value().keys()) {
        const auto name = nameStr.toLocal8Bit();
        if (!isShellVariable(name) && !isSessionVariable(name)) {
            setEnvironmentVariable(name, environment.value().value(nameStr).toLocal8Bit());
        }
    }
}

// Source scripts found in <config locations>/lingmo-workspace/env/*.sh
// (where <config locations> correspond to the system and user's configuration
// directory.
//
// Scripts are sourced in reverse order of priority of their directory, as defined
// by `QStandardPaths::standardLocations`. This ensures that high-priority scripts
// (such as those in the user's home directory) are sourced last and take precedence
// over lower-priority scripts (such as system defaults). Scripts in the same
// directory are sourced in lexical order of their filename.
//
// This is where you can define environment variables that will be available to
// all KDE programs, so this is where you can run agents using e.g. eval `ssh-agent`
// or eval `gpg-agent --daemon`.
// Note: if you do that, you should also put "ssh-agent -k" as a shutdown script
//
// (see end of this file).
// For anything else (that doesn't set env vars, or that needs a window manager),
// better use the Autostart folder.

void runEnvironmentScripts()
{
    QStringList scripts;
    auto locations = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);

    //`standardLocations()` returns locations sorted by "order of priority". We iterate in reverse
    // order so that high-priority scripts are sourced last and their modifications take precedence.
    for (auto loc = locations.crbegin(); loc != locations.crend(); loc++) {
        QDir dir(*loc);
        if (!dir.cd(QStringLiteral("./lingmo-workspace/env"))) {
            // Skip location if lingmo-workspace/env subdirectory does not exist
            continue;
        }
        const auto dirScripts = dir.entryInfoList({QStringLiteral("*.sh")}, QDir::Files, QDir::Name);
        for (const auto &script : dirScripts) {
            scripts << script.absoluteFilePath();
        }
    }
    sourceFiles(scripts);
}

// Mark that full KDE session is running (e.g. Konqueror preloading works only
// with full KDE running). The KDE_FULL_SESSION property can be detected by
// any X client connected to the same X session, even if not launched
// directly from the KDE session but e.g. using "ssh -X", kdesu. $KDE_FULL_SESSION
// however guarantees that the application is launched in the same environment
// like the KDE session and that e.g. KDE utilities/libraries are available.
// KDE_FULL_SESSION property is also only available since KDE 3.5.5.
// The matching tests are:
//   For $KDE_FULL_SESSION:
//     if test -n "$KDE_FULL_SESSION"; then ... whatever
//   For KDE_FULL_SESSION property (on X11):
//     xprop -root | grep "^KDE_FULL_SESSION" >/dev/null 2>/dev/null
//     if test $? -eq 0; then ... whatever
//
// Additionally there is $KDE_SESSION_UID with the uid
// of the user running the KDE session. It should be rarely needed (e.g.
// after sudo to prevent desktop-wide functionality in the new user's kded).
//
// Since KDE4 there is also KDE_SESSION_VERSION, containing the major version number.
//

void setupPlasmaEnvironment()
{
    // Manually disable auto scaling because we are scaling above
    // otherwise apps that manually opt in for high DPI get auto scaled by the developer AND manually scaled by us
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");

    qputenv("KDE_FULL_SESSION", "true");
    qputenv("KDE_SESSION_VERSION", "5");
    qputenv("KDE_SESSION_UID", QByteArray::number(getuid()));
    qputenv("XDG_CURRENT_DESKTOP", "KDE");

    qputenv("KDE_APPLICATIONS_AS_SCOPE", "1");

    // Add kdedefaults dir to allow config defaults overriding from a writable location
    QByteArray currentConfigDirs = qgetenv("XDG_CONFIG_DIRS");
    if (currentConfigDirs.isEmpty()) {
        currentConfigDirs = "/etc/xdg";
    }
    const QString extraConfigDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/kdedefaults");
    QDir().mkpath(extraConfigDir);
    qputenv("XDG_CONFIG_DIRS", QFile::encodeName(extraConfigDir) + ':' + currentConfigDirs);

    const KConfig globals;
    const QString currentLnf = KConfigGroup(&globals, QStringLiteral("KDE")).readEntry("LookAndFeelPackage", QStringLiteral("org.lingmo.ocean.desktop"));
    QFile activeLnf(extraConfigDir + QLatin1String("/package"));
    activeLnf.open(QIODevice::ReadOnly);
    if (activeLnf.readLine() != currentLnf.toUtf8()) {
        KPackage::Package package = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/LookAndFeel"), currentLnf);
        LookAndFeelManager lnfManager;
        lnfManager.setMode(LookAndFeelManager::Mode::Defaults);
        lnfManager.save(package, KPackage::Package());
    }
    // check if colors changed, if so apply them and discard palsma cache
    {
        LookAndFeelManager lnfManager;
        lnfManager.setMode(LookAndFeelManager::Mode::Apply);
        KConfig globals(QStringLiteral("kdeglobals")); // Reload the config
        KConfigGroup generalGroup(&globals, QStringLiteral("General"));
        const QString colorScheme = generalGroup.readEntry("ColorScheme", QStringLiteral("Ocean"));
        QString path = lnfManager.colorSchemeFile(colorScheme);

        if (!path.isEmpty()) {
            QFile f(path);
            QCryptographicHash hash(QCryptographicHash::Sha1);
            if (f.open(QFile::ReadOnly) && hash.addData(&f)) {
                const QString fileHash = QString::fromUtf8(hash.result().toHex());
                if (fileHash != generalGroup.readEntry("ColorSchemeHash", QString())) {
                    lnfManager.setColors(colorScheme, path);
                    generalGroup.writeEntry("ColorSchemeHash", fileHash);
                    generalGroup.sync();
                    const QString svgCache =
                        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1Char('/') + QStringLiteral("lingmo-svgelements");
                    if (!svgCache.isEmpty()) {
                        QFile::remove(svgCache);
                    }
                }
            }
        }
    }
}

void setupX11()
{
    //     Set a left cursor instead of the standard X11 "X" cursor, since I've heard
    //     from some users that they're confused and don't know what to do. This is
    //     especially necessary on slow machines, where starting KDE takes one or two
    //     minutes until anything appears on the screen.
    //
    //     If the user has overwritten fonts, the cursor font may be different now
    //     so don't move this up.

    runSync(QStringLiteral("xsetroot"), {QStringLiteral("-cursor_name"), QStringLiteral("left_ptr")});
}

void cleanupPlasmaEnvironment(const std::optional<QProcessEnvironment> &oldSystemdEnvironment)
{
    qunsetenv("KDE_FULL_SESSION");
    qunsetenv("KDE_SESSION_VERSION");
    qunsetenv("KDE_SESSION_UID");

    if (!oldSystemdEnvironment) {
        return;
    }

    auto currentEnv = getSystemdEnvironment();
    if (!currentEnv) {
        return;
    }

    // According to systemd documentation:
    // If a variable is listed in both, the variable is set after this method returns, i.e. the set list overrides the unset list.
    // So this will effectively restore the state to the values in oldSystemdEnvironment.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("UnsetAndSetEnvironment"));
    message.setArguments({currentEnv.value().keys(), oldSystemdEnvironment.value().toStringList()});

    // The session program gonna quit soon, ensure the message is flushed.
    auto reply = QDBusConnection::sessionBus().asyncCall(message);
    reply.waitForFinished();
}

// Drop session-specific variables from the systemd environment.
// Those can be leftovers from previous sessions, which can interfere with the session
// we want to start now, e.g. $DISPLAY might break kwin_wayland.
static void dropSessionVarsFromSystemdEnvironment()
{
    const auto environment = getSystemdEnvironment();
    if (!environment) {
        return;
    }

    QStringList varsToDrop;
    for (auto &nameStr : environment.value().keys()) {
        // If it's set in this process, it'll be overwritten by the following UpdateLaunchEnvJob
        const auto name = nameStr.toLocal8Bit();
        if (!qEnvironmentVariableIsSet(name) && isSessionVariable(name)) {
            varsToDrop.append(nameStr);
        }
    }

    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                              QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.systemd1.Manager"),
                                              QStringLiteral("UnsetEnvironment"));
    msg << varsToDrop;
    auto reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(PLASMA_STARTUP) << "Failed to unset systemd environment variables:" << reply.errorName() << reply.errorMessage();
    }
}

// kwin_wayland can possibly also start dbus-activated services which need env variables.
// In that case, the update in startplasma might be too late.
bool syncDBusEnvironment()
{
    dropSessionVarsFromSystemdEnvironment();

    // Shell variables are filtered out of things we explicitly load, but they
    // still might have been inherited from the parent process
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto &name : environment.keys()) {
        if (isShellVariable(name.toLocal8Bit())) {
            environment.remove(name);
        }
    }

    // At this point all environment variables are set, let's send it to the DBus session server to update the activation environment
    auto job = new UpdateLaunchEnvJob(environment);
    return job->exec();
}

static bool desktopLockedAtStart = false;

QProcess *setupKSplash()
{
    const auto dlstr = qgetenv("DESKTOP_LOCKED");
    desktopLockedAtStart = dlstr == "true" || dlstr == "1";
    qunsetenv("DESKTOP_LOCKED"); // Don't want it in the environment

    QProcess *p = nullptr;
    if (!desktopLockedAtStart) {
        const KConfig cfg(QStringLiteral("ksplashrc"));
        // the splashscreen and progress indicator
        KConfigGroup ksplashCfg = cfg.group("KSplash");
        if (ksplashCfg.readEntry("Engine", QStringLiteral("KSplashQML")) == QLatin1String("KSplashQML")) {
            p = new QProcess;
            p->setProcessChannelMode(QProcess::ForwardedChannels);
            p->start(QStringLiteral("ksplashqml"), {ksplashCfg.readEntry("Theme", QStringLiteral("Breeze"))});
        }
    }
    return p;
}

// If something went on an endless restart crash loop it will get blacklisted, as this is a clean login we will want to reset those counters
// This is independent of whether we use the Plasma systemd boot
void resetSystemdFailedUnits()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("ResetFailed"));
    QDBusConnection::sessionBus().call(message);
}

// Reload systemd to make sure the current configuration is active, which also reruns generators.
// Needed for e.g. XDG autostart changes to become effective.
void reloadSystemd()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("Reload"));
    QDBusConnection::sessionBus().call(message);
}

bool hasSystemdService(const QString &serviceName)
{
    qDBusRegisterMetaType<QPair<QString, QString>>();
    qDBusRegisterMetaType<QList<QPair<QString, QString>>>();
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                              QStringLiteral("/org/freedesktop/systemd1"),
                                              QStringLiteral("org.freedesktop.systemd1.Manager"),
                                              QStringLiteral("ListUnitFilesByPatterns"));
    msg << QStringList({QStringLiteral("enabled"), QStringLiteral("static"), QStringLiteral("linked"), QStringLiteral("linked-runtime")});
    msg << QStringList({serviceName});
    QDBusReply<QList<QPair<QString, QString>>> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) {
        return false;
    }
    // if we have a service returned then it must have found it
    return !reply.value().isEmpty();
}

bool useSystemdBoot()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("startshellrc"), KConfig::NoGlobals);
    const QString configValue = config->group(QStringLiteral("General")).readEntry("systemdBoot", QStringLiteral("true")).toLower();

    if (configValue == QLatin1String("false")) {
        return false;
    }

    if (configValue == QLatin1String("force")) {
        qInfo() << "Systemd boot forced";
        return true;
    }

    if (!hasSystemdService(QStringLiteral("lingmo-workspace.target"))) {
        return false;
    }

    // xdg-desktop-autostart.target is shipped with an systemd 246 and provides a generator
    // for creating units out of existing autostart files
    // only enable our systemd boot if that exists, unless the user has forced the systemd boot above
    return hasSystemdService(QStringLiteral("xdg-desktop-autostart.target"));
}

void startKSplashViaSystemd()
{
    const KConfig cfg(QStringLiteral("ksplashrc"));
    // the splashscreen and progress indicator
    KConfigGroup ksplashCfg = cfg.group("KSplash");
    if (ksplashCfg.readEntry("Engine", QStringLiteral("KSplashQML")) == QLatin1String("KSplashQML")) {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StartUnit"));
        msg << QStringLiteral("lingmo-ksplash.service") << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
    }
}

static void migrateUserScriptsAutostart()
{
    QDir configLocation(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    QDir autostartScriptsLocation(configLocation.filePath(QStringLiteral("autostart-scripts")));
    if (!autostartScriptsLocation.exists()) {
        return;
    }
    const QDir autostartScriptsMovedLocation(configLocation.filePath(QStringLiteral("old-autostart-scripts")));
    const auto entries = autostartScriptsLocation.entryInfoList(QDir::Files);
    for (const auto &info : entries) {
        const auto scriptName = info.fileName();
        const auto scriptPath = info.absoluteFilePath();
        const auto scriptMovedPath = autostartScriptsMovedLocation.filePath(scriptName);

        // Don't migrate backup files
        if (scriptName.endsWith(QLatin1Char('~')) || scriptName.endsWith(QLatin1String(".bak"))
            || (scriptName[0] == QLatin1Char('%') && scriptName.endsWith(QLatin1Char('%')))
            || (scriptName[0] == QLatin1Char('#') && scriptName.endsWith(QLatin1Char('#')))) {
            qCDebug(PLASMA_STARTUP) << "Not migrating backup autostart script" << scriptName;
            continue;
        }

        // Migrate autostart script to a standard .desktop autostart file
        AutostartScriptDesktopFile desktopFile(scriptName, info.isSymLink() ? info.symLinkTarget() : scriptMovedPath);
        qCInfo(PLASMA_STARTUP) << "Migrated legacy autostart script" << scriptPath << "to" << desktopFile.fileName();

        if (info.isSymLink() && QFile::remove(scriptPath)) {
            qCInfo(PLASMA_STARTUP) << "Removed legacy autostart script" << scriptPath << "that pointed to" << info.symLinkTarget();
        }
    }
    // Delete or rename autostart-scripts to old-autostart-scripts to avoid running the migration again
    if (autostartScriptsLocation.entryInfoList(QDir::Files).empty()) {
        autostartScriptsLocation.removeRecursively();
    } else {
        configLocation.rename(autostartScriptsLocation.dirName(), autostartScriptsMovedLocation.dirName());
    }
    // Reload systemd so that the XDG autostart generator is run again to pick up the new .desktop files
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("Reload"));
    QDBusConnection::sessionBus().call(message);
}

bool startPlasmaSession(bool wayland)
{
    resetSystemdFailedUnits();
    reloadSystemd();
    OrgKdeKSplashInterface iface(QStringLiteral("org.kde.KSplash"), QStringLiteral("/KSplash"), QDBusConnection::sessionBus());
    iface.setStage(QStringLiteral("startPlasma"));
    // finally, give the session control to the session manager
    // see kdebase/ksmserver for the description of the rest of the startup sequence
    // if the KDEWM environment variable has been set, then it will be used as KDE's
    // window manager instead of kwin.
    // if KDEWM is not set, ksmserver will ensure kwin is started.
    // kwrapper5 is used to reduce startup time and memory usage
    // kwrapper5 does not return useful error codes such as the exit code of ksmserver.
    // We only check for 255 which means that the ksmserver process could not be
    // started, any problems thereafter, e.g. ksmserver failing to initialize,
    // will remain undetected.
    // If the session should be locked from the start (locked autologin),
    // lock now and do the rest of the KDE startup underneath the locker.

    bool rc = true;
    QEventLoop e;

    QDBusServiceWatcher serviceWatcher;
    serviceWatcher.setConnection(QDBusConnection::sessionBus());

    // We want to exit when both ksmserver and lingmo-session-shutdown have finished
    // This also closes if ksmserver crashes unexpectedly, as in those cases lingmo-poweroff is not running
    if (wayland) {
        serviceWatcher.addWatchedService(QStringLiteral("org.kde.KWinWrapper"));
    } else {
        serviceWatcher.addWatchedService(QStringLiteral("org.kde.ksmserver"));
    }
    serviceWatcher.addWatchedService(QStringLiteral("org.kde.Shutdown"));
    serviceWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    QObject::connect(&serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, [&]() {
        const QStringList watchedServices = serviceWatcher.watchedServices();
        bool plasmaSessionRunning = std::any_of(watchedServices.constBegin(), watchedServices.constEnd(), [](const QString &service) {
            return QDBusConnection::sessionBus().interface()->isServiceRegistered(service);
        });
        if (!plasmaSessionRunning) {
            e.quit();
        }
    });

    // Create .desktop files for the scripts in .config/autostart-scripts
    migrateUserScriptsAutostart();

    std::unique_ptr<QProcess, KillBeforeDeleter> startPlasmaSession;
    if (!useSystemdBoot()) {
        startPlasmaSession.reset(new QProcess);
        qCDebug(PLASMA_STARTUP) << "Using classic boot";

        QStringList plasmaSessionOptions;
        if (wayland) {
            plasmaSessionOptions << QStringLiteral("--no-lockscreen");
        } else {
            if (desktopLockedAtStart) {
                plasmaSessionOptions << QStringLiteral("--lockscreen");
            }
        }

        startPlasmaSession->setProcessChannelMode(QProcess::ForwardedChannels);
        QObject::connect(startPlasmaSession.get(), &QProcess::finished, &e, [&rc](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 255) {
                // Startup error
                messageBox(QStringLiteral("startshell: Could not start plasma_session. Check your installation.\n"));
                rc = false;
            }
        });

        startPlasmaSession->start(QStringLiteral(CMAKE_INSTALL_FULL_BINDIR "/plasma_session"), plasmaSessionOptions);
    } else {
        qCDebug(PLASMA_STARTUP) << "Using systemd boot";
        const QString platform = wayland ? QStringLiteral("wayland") : QStringLiteral("x11");

        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                  QStringLiteral("/org/freedesktop/systemd1"),
                                                  QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("StartUnit"));
        msg << QStringLiteral("lingmo-workspace-%1.target").arg(platform) << QStringLiteral("fail");
        QDBusReply<QDBusObjectPath> reply = QDBusConnection::sessionBus().call(msg);
        if (!reply.isValid()) {
            qCWarning(PLASMA_STARTUP) << "Could not start systemd managed Plasma session:" << reply.error().name() << reply.error().message();
            messageBox(QStringLiteral("startshell: Could not start Plasma session.\n"));
            rc = false;
        } else {
            playStartupSound();
        }
        if (wayland) {
            startKSplashViaSystemd();
        }
    }
    if (rc) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &e, &QEventLoop::quit);
        e.exec();
    }
    return rc;
}

void waitForKonqi()
{
    const KConfig cfg(QStringLiteral("startshellrc"));
    const KConfigGroup grp = cfg.group("WaitForDrKonqi");
    bool wait_drkonqi = grp.readEntry("Enabled", true);
    if (wait_drkonqi) {
        // wait for remaining drkonqi instances with timeout (in seconds)
        const int wait_drkonqi_timeout = grp.readEntry("Timeout", 900) * 1000;
        QElapsedTimer wait_drkonqi_counter;
        wait_drkonqi_counter.start();
        QStringList services = allServices(QLatin1String("org.kde.drkonqi-"));
        while (!services.isEmpty()) {
            sleep(5);
            services = allServices(QLatin1String("org.kde.drkonqi-"));
            if (wait_drkonqi_counter.elapsed() >= wait_drkonqi_timeout) {
                // ask remaining drkonqis to die in a graceful way
                for (const auto &service : qAsConst(services)) {
                    QDBusInterface iface(service, QStringLiteral("/MainApplication"));
                    iface.call(QStringLiteral("quit"));
                }
                break;
            }
        }
    }
}

void playStartupSound()
{
    KNotifyConfig notifyConfig(QStringLiteral("plasma_workspace"), QList<QPair<QString, QString>>(), QStringLiteral("startshell"));
    const QString action = notifyConfig.readEntry(QStringLiteral("Action"));
    if (action.isEmpty() || !action.split(QLatin1Char('|')).contains(QLatin1String("Sound"))) {
        // no startup sound configured
        return;
    }
    Phonon::AudioOutput *audioOutput = new Phonon::AudioOutput(Phonon::NotificationCategory);

    QString soundFilename = notifyConfig.readEntry(QStringLiteral("Sound"));
    if (soundFilename.isEmpty()) {
        qCWarning(PLASMA_STARTUP) << "Audio notification requested, but no sound file provided in notifyrc file, aborting audio notification";
        audioOutput->deleteLater();
        return;
    }

    QUrl soundURL;
    const auto dataLocations = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataLocation : dataLocations) {
        soundURL = QUrl::fromUserInput(soundFilename, dataLocation + QStringLiteral("/sounds"), QUrl::AssumeLocalFile);
        if (soundURL.isLocalFile() && QFile::exists(soundURL.toLocalFile())) {
            break;
        } else if (!soundURL.isLocalFile() && soundURL.isValid()) {
            break;
        }
        soundURL.clear();
    }
    if (soundURL.isEmpty()) {
        qCWarning(PLASMA_STARTUP) << "Audio notification requested, but sound file from notifyrc file was not found, aborting audio notification";
        audioOutput->deleteLater();
        return;
    }

    Phonon::MediaObject *mediaObject = new Phonon::MediaObject();
    Phonon::createPath(mediaObject, audioOutput);
    QObject::connect(mediaObject, &Phonon::MediaObject::finished, audioOutput, &QObject::deleteLater);
    QObject::connect(mediaObject, &Phonon::MediaObject::finished, mediaObject, &QObject::deleteLater);

    mediaObject->setCurrentSource(soundURL);
    mediaObject->play();
}
