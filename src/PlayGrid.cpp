/*
 * Copyright (C) 2021 Dan Leinir Turthra Jensen <admin@leinir.dk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "PlayGrid.h"

#include <QDir>

class PlayGrid::Private
{
public:
    Private() {}
    QString name;
    QString getDataDir()
    {
        // test and make sure that this env var contains something, or spit out .local/zynthian or something
        return QString("%1/playgrid/%2").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR"))).arg(name);
    }

    QString getSafeFilename(const QString& unsafe)
    {
        QStringList keepcharacters{" ",".","_"};
        QString safe;
        for (const QChar &letter : unsafe) {
            if (letter.isLetterOrNumber() || keepcharacters.contains(letter)) {
                safe.append(letter);
            }
        }
        return getDataDir() + "/" + safe;
    }
};

PlayGrid::PlayGrid(QQuickItem* parent)
    : QQuickItem(parent)
    , d(new Private)
{
}

PlayGrid::~PlayGrid()
{
    delete d;
}

QString PlayGrid::loadData(const QString& key)
{
    QString data;
    QFile file(d->getSafeFilename(key));
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    return data;
}

bool PlayGrid::saveData(const QString& key, const QString& data)
{
    bool success = false;
    QDir confLocation(d->getDataDir());
    if (confLocation.exists() || confLocation.mkpath(confLocation.path())) {
        QFile dataFile(d->getSafeFilename(key));
        if (dataFile.write(data.toUtf8())) {
            success = true;
        }
    }
    return success;
}
