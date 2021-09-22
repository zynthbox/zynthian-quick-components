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

#include "SettingsContainer.h"

#include <QHash>

class SettingsContainer::Private
{
public:
    Private() {}
    QString name;
    QHash<QString, QVariant> entries;
};

SettingsContainer::SettingsContainer(QString name, QObject* parent)
    : QObject(parent)
    , d(new Private)
{
    d->name = name;
}

SettingsContainer::~SettingsContainer()
{
    delete d;
}

QString SettingsContainer::name() const
{
    return d->name;
}

QVariant SettingsContainer::getProperty(const QString& property) const
{
    if (hasProperty(property)) {
        return d->entries[property];
    }
    return QVariant();
}

void SettingsContainer::setProperty(const QString& property, const QVariant& value)
{
    d->entries[property] = value;
}

void SettingsContainer::clearProperty(const QString& property)
{
    d->entries.remove(property);
}

bool SettingsContainer::hasProperty(const QString& property) const
{
    return d->entries.contains(property);
}
