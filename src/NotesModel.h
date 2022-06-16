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
#include "PlayGridManager.h"

class NotesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int rows READ rowCount NOTIFY rowsChanged)
    Q_PROPERTY(QVariantMap roles READ roles CONSTANT)
    Q_PROPERTY(QObject* parentModel READ parentModel CONSTANT)
    Q_PROPERTY(int parentRow READ parentRow NOTIFY parentRowChanged)
    Q_PROPERTY(quint64 lastModified READ lastModified NOTIFY lastModifiedChanged);
    /**
     * \brief Whether or not there are any notes anywhere in the model
     */
    Q_PROPERTY(bool isEmpty READ isEmpty NOTIFY isEmptyChanged);
public:
    explicit NotesModel(PlayGridManager *parent = nullptr);
    explicit NotesModel(NotesModel *parent, int row);
    ~NotesModel() override;

    enum Roles {
        NoteRole = Qt::UserRole + 1,
        MetadataRole,
        RowModelRole,
    };
    QVariantMap roles() const;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;

    /**
     * \brief Get the parent model for this model
     * For ease of use, you can use the rowModel role to fetch a child model of a specific row,
     * and from that row you can then use parentModel to interact with that parent.
     * @see parentRow()
     * @return A NotesModel instance, or null if it is a root model
     */
    Q_INVOKABLE QObject *parentModel() const;
    /**
     * \brief The row this model exists within in the parent model
     * For ease of use, you can use the rowModel role to fetch a child model of a specific row,
     * and the parentRow call on that child model will return the row it represents in the parent.
     * @return The row of this model (-1 if it is a root model)
     */
    Q_INVOKABLE int parentRow() const;
    Q_SIGNAL void parentRowChanged();
    /**
     * \brief When the last change was made on the model (setting notes, metadata, or anything else really)
     * @return The timestamp of the most recent change, to the nearest second
     */
    quint64 lastModified() const;
    Q_SIGNAL void lastModifiedChanged();
    /**
     * \brief Call this to make the object notice that a change has happened (changing lastModified)
     */
    void registerChange();

    bool isEmpty() const;
    Q_SIGNAL void isEmptyChanged();

    /**
     * \brief Get a list with all the notes in the specified row
     * @note Not valid on child models (see parentModel())
     * @param row The row you wish to get a list of notes from
     * @return A list containing all the notes for a specific row
     */
    Q_INVOKABLE QVariantList getRow(int row) const;

    /**
     * \brief Get a list of all unique notes in the given row
     * @note Not valid on child models (see parentModel())
     * @param row The row to get notes from
     */
    Q_INVOKABLE QVariantList uniqueRowNotes(int row) const;

    /**
     * \brief Get the note object stored at the specified location
     * @note Not valid on child models (see parentModel())
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
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to set to the given note
     * @param column The column of the position to set to the given note
     * @param note The new note to be set in the specified location (this may be null, to clear the position)
     */
    Q_INVOKABLE virtual void setNote(int row, int column, QObject *note);
    /**
     * \brief Get the metadata instances for an entire row (matching the positions in the row)
     * @note Not valid on child models (see parentModel())
     * @param row The row you wish to get a list of metadata from
     * @return The list of metadata
     */
    Q_INVOKABLE QVariantList getRowMetadata(int row) const;
    /**
     * \brief Retrieve the metadata set for the given position
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to fetch metadata for
     * @param column The column of the position to fetch metadata for
     * @return A QVariant which contains the metadata (this may be invalid if there was none set for the position)
     */
    Q_INVOKABLE QVariant getMetadata(int row, int column) const;
    /**
     * \brief Set an abstract piece of metadata for the given position
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to set metadata for
     * @param column The column of the position to set the metadata for
     * @param metadata The piece of metadata you wish to set
     */
    Q_INVOKABLE virtual void setMetadata(int row, int column, QVariant metadata);
    /**
     * \brief Set a piece of named metadata for the given position
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to set the keyed metadata for
     * @param column The column of the position to set the keyed metadata for
     * @param key The name for the piece of metadata
     * @param value The piece of metadata you wish to set (pass an empty string to unset the key)
     */
    Q_INVOKABLE void setKeyedMetadata(int row, int column, const QString& key, const QVariant& metadata);
    /**
     * \brief Get a piece of named metadata for the given position
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to fetch the keyed metadata from
     * @param column The column of the position to fetch the keyed metadata from
     * @parm key The name of the piece of metadata
     * @return A QVariant, which is invalid if the key doesn't exist, and otherwise contains the value
     */
    Q_INVOKABLE QVariant getKeyedMetadata(int row, int column, const QString& key) const;
    /**
     * \brief Get all the hash containing all the keyed data for the given position
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to fetch all keyed data from
     * @param column The column of the position to fetch all keyed data from
     * @return A QVariantHash
     */
    Q_INVOKABLE QVariantHash getKeyedData(int row, int column) const;

    /**
     * \brief Set the list of notes and metadata for the given row to be the given list
     * @note This will reset the size of this row
     * @param row The row you wish to replace the notes for
     * @param notes The list of notes you wish to set for the given row
     * @param metadata The list of metadata you wish to set for the given row
     * @param keyedData The list of keyed data you wish to set for the given row
     */
    Q_INVOKABLE void setRowData(int row, QVariantList notes, QVariantList metadata = QVariantList(), QVariantList keyedData = QVariantList());

    /**
     * \brief Trims the rows in the model of all trailing empty columns, and removes any empty rows
     * @note Not valid on child models (see parentModel())
     * @note This will disregard metadata set on any trailing fields (only notes are counted as the model being filled)
     */
    Q_INVOKABLE void trim();

    /**
     * \brief Remove all rows from the model
     * @note Not valid on child models (see parentModel())
     */
    Q_INVOKABLE virtual void clear();
    /**
     * \brief Add a new row of notes to the top of the model
     * @note Not valid on child models (see parentModel())
     * @see appendRow for adding to the end
     * @param notes A list of notes to be added to the model as a new row (they will be inserted at the top of the model)
     * @param metadata An optional list of metadata associated with the notes
     */
    Q_INVOKABLE void addRow(const QVariantList &notes, const QVariantList &metadata = QVariantList());
    /**
     * \brief Add a new row of notes to the end of the model
     * @note Not valid on child models (see parentModel())
     * @see addRow() for adding at the top
     * @param notes A list of notes to be added to the model as a new row (they will be inserted at the top of the model)
     * @param metadata An optional list of metadata associated with the notes
     */
    Q_INVOKABLE void appendRow(const QVariantList &notes, const QVariantList &metadata = QVariantList());
    /**
     * \brief Insert a row of notes at the specified position in the model
     * @note Not valid on child models (see parentModel())
     * @see addRow() for adding at the top
     * @param index The location at which you wish to insert the row
     * @param notes A list of notes to be added to the model as a new row (they will be inserted at the top of the model)
     * @param metadata An optional list of metadata associated with the notes
     * @param keyedData An optional hash of key/value based data associated with the notes
     */
    Q_INVOKABLE void insertRow(int index, const QVariantList &notes, const QVariantList &metadata = QVariantList(), const QVariantList &keyedData = QVariantList());

    /**
     * \brief Remove a row of notes from the model
     * @note Not valid on child models (see parentModel())
     * @param row The row that you wish to remove
     */
    Q_INVOKABLE void removeRow(int row);

    /**
     * \brief Changes all notes in the model to switch to the given midi channel
     * This is a convenience function, useful for when nothing else changes (say the global midi
     * channel changes and you still want to be able to use this model for handling notes on that
     * channel, such as in the basic Notes playground module)
     * @note If this is called with an invalid midi channel, it will be clamped to -1 and 16 (still invalid, but don't use this for information storage outside of "invalid")
     * @param midiChannel The midi channel (0 through 15) that all notes and subnotes (etc) in the model should use
     */
    Q_INVOKABLE void changeMidiChannel(int midiChannel);
    /**
     * \brief Get the PlayGridManager instance associated with this model
     * @return The PlayGridManager associated with this model (either the immediate parent, or the one from the parent model)
     */
    PlayGridManager *playGridManager() const;

    Q_SIGNAL void rowsChanged();

    /**
     * \brief Call this before starting an operation that will do more than one change
     * This will stop updates from being emitted by the model, until a corresponding endLongOperation
     * call is made. You can nest multiple of these, and as such it is safe to use.
     * @note You /must/ make a matching end call, or the model will basically stop working correctly
     */
    Q_INVOKABLE void startLongOperation();
    /**
     * \brief Call this after completing an operation that will do more than one change
     * @see startLongOperation()
     */
    Q_INVOKABLE void endLongOperation();
private:
    class Private;
    Private* d;
};

#endif//NOTESMODEL_H
