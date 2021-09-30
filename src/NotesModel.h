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
        MetadataRole,
    };
    QVariantMap roles() const;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;

    /**
     * \brief Get the note object stored at the specified location
     * @param row The row to look in
     * @param column The column of that row to look in
     * @return Either a Note object, or null if there was no Note stored in that position
     */
    Q_INVOKABLE QObject *getNote(int row, int column) const;
    /**
     * \brief Set the indicated position in the model to the given note
     * This sets a specified location to contain the Note object passed to the function. If the location does
     * not yet exist, rows will be appended to the model until there are that many rows, and column added to
     * the row until the position exists. Clearing the position will not remove the position from the model.
     * @param row The row of the position to set to the given note
     * @param column The column of the position to set to the given note
     * @param note The new note to be set in the specified location (this may be null, to clear the position)
     */
    Q_INVOKABLE void setNote(int row, int column, QObject *note);
    /**
     * \brief Retrieve the metadata set for the given position
     * @param row The row of the position to fetch metadata for
     * @param column The column of the position to fetch metadata for
     * @return A QVariant which contains the metadata (this may be invalid if there was none set for the position)
     */
    Q_INVOKABLE QVariant getMetadata(int row, int column) const;
    /**
     * \brief Set an abstract piece of metadata for the given position
     * @param row The row of the position to set metadata for
     * @param column The column of the position to set the metadata for
     * @param metadata The piece of metadata you wish to set
     */
    Q_INVOKABLE void setMetadata(int row, int column, QVariant metadata);
    /**
     * \brief Trims the rows in the model of all trailing empty columns, and removes any empty rows
     * \note This will disregard metadata set on any trailing fields (only notes are counted as the model being filled)
     */
    Q_INVOKABLE void trim();

    /**
     * \brief Remove all rows from the model
     */
    Q_INVOKABLE void clear();
    /**
     * \brief Add a new row of notes to the model
     * @param notes A list of notes to be added to the model as a new row (they will be inserted at the top of the model)
     */
    Q_INVOKABLE void addRow(const QVariantList &notes);

    Q_SIGNAL void rowsChanged();
private:
    class Private;
    Private* d;
};

#endif//NOTESMODEL_H
