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

#ifndef NOTESMODEL_H
#define NOTESMODEL_H

#include <QAbstractListModel>

class NotesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int rows READ rowCount NOTIFY rowsChanged)
    Q_PROPERTY(QVariantMap roles READ roles CONSTANT)
public:
    explicit NotesModel(QObject *parent = nullptr);
    ~NotesModel() override;

    enum Roles {
        NoteRole = Qt::UserRole + 1,
    };
    QVariantMap roles() const;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;

    Q_SIGNAL void rowsChanged();

    Q_INVOKABLE void clear();
    Q_INVOKABLE void addRow(const QVariantList &notes);
private:
    class Private;
    Private* d;
};

#endif//NOTESMODEL_H
