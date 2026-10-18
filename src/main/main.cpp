/* ============================================================
* Falkon - Qt web browser
* Copyright (C) 2010-2017 David Rosca <nowrep@gmail.com>
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
#include "proxystyle.h"

#include <iostream>

#ifndef Q_OS_WIN
void msgHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
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

int main(int argc, char* argv[])
{
#ifndef Q_OS_WIN
    qInstallMessageHandler(&msgHandler);
#endif

    // Headless 渲染无显示环境：默认 offscreen 平台（可显式覆盖 QT_QPA_PLATFORM）
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        for (int i = 1; i < argc; ++i) {
            if (qstrcmp(argv[i], "--headless-render") == 0) {
                qputenv("QT_QPA_PLATFORM", "offscreen");
                break;
            }
        }
    }

    // Hack to fix QT_STYLE_OVERRIDE with QProxyStyle
    const QByteArray style = qgetenv("QT_STYLE_OVERRIDE");
    if (!style.isEmpty()) {
        char** args = (char**) malloc(sizeof(char*) * (argc + 1));
        if (args != NULL) {
            for (int i = 0; i < argc; ++i)
                args[i] = argv[i];

            QString stylecmd = QL1S("-style=") + QString::fromUtf8(style);
            args[argc++] = qstrdup(stylecmd.toUtf8().constData());
            argv = args;
        }
        else {
            qWarning() << "main: Unable to allocate memory to apply a hack around QT_STYLE_OVERRIDE.";
        }
    }

    MainApplication app(argc, argv);

    if (app.isClosing())
        return 0;

    app.setProxyStyle(new ProxyStyle);

    return app.exec();
}
