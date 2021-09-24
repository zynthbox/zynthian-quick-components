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

#ifndef PLAYGRID_H
#define PLAYGRID_H

#include <QQuickItem>

class PlayGrid : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* playGridManager READ playGridManager WRITE setPlayGridManager NOTIFY playGridManagerChanged)
public:
    explicit PlayGrid(QQuickItem *parent = nullptr);
    ~PlayGrid() override;

    /**
     * \brief Load a string value saved to disk under a specified name
     * @param key The name of the data you wish to retrieve
     * @return A string containing the data contained in the specified key (an empty string if none was found)
     */
    Q_INVOKABLE QString loadData(const QString &key);
    /**
     * \brief Save a string value to disk under a specified name
     *
     * @note The key will be turned into a filesystem-safe string before attempting to save the data
     *       to disk. This also means that if you are overly clever with naming, you may end up with
     *       naming clashes. In other words, be sensible in naming your keys, and your behaviour will
     *       be more predictable.
     * @param key The name of the data you wish to store
     * @param data The contents you wish to store, in string form
     * @return True if successful, false if unsuccessful
     */
    Q_INVOKABLE bool saveData(const QString &key, const QString &data);

    QObject *playGridManager() const;
    void setPlayGridManager(QObject *playGridManager);
    Q_SIGNAL void playGridManagerChanged();
private:
    class Private;
    Private *d;
};

#endif//PLAYGRID_H
