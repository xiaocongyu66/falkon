/* ============================================================
* Falkon - Qt web browser
* Copyright (C) 2010-2014  David Rosca <nowrep@gmail.com>
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
#include "acceptlanguage.h"

QStringList AcceptLanguage::defaultLanguage()
{
    QString longCode = QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));

    if (longCode.size() == 5) {
        QStringList ret;
        ret << longCode << longCode.left(2);
        return ret;
    }

    return QStringList(longCode);
}

QByteArray AcceptLanguage::generateHeader(const QStringList &langs)
{
    if (langs.isEmpty()) {
        return {};
    }

    QByteArray header;
    header.append(langs.at(0).toLatin1());

    int counter = 8;
    for (int i = 1; i < langs.count(); i++) {
        QString s = QSL(",") + langs.at(i) + QSL(";q=0.");
        s.append(QString::number(counter));
        if (counter != 2) {
            counter -= 2;
        }

        header.append(s.toLatin1());
    }

    return header;
}
