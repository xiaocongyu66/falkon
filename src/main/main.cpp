/* ============================================================
* Falkon - Qt web browser (headless render fork)
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

int main(int argc, char* argv[])
{
    // Headless rendering without a display: default to the offscreen platform
    // (can be explicitly overridden via QT_QPA_PLATFORM)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        for (int i = 1; i < argc; ++i) {
            if (qstrcmp(argv[i], "--headless-render") == 0) {
                qputenv("QT_QPA_PLATFORM", "offscreen");
                break;
            }
        }
    }

    MainApplication app(argc, argv);

    if (app.isClosing())
        return 0;

    return app.exec();
}
