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

#ifndef PATTERNMODEL_H
#define PATTERNMODEL_H

#include "NotesModel.h"

/**
 * \brief A way to keep track of the notes which make up a conceptual song pattern
 *
 * This specialised NotesModel will always be a square model (that is, all rows have the same width).
 *
 * Each position in the model contains a compound note, and the metadata associated with the subnotes,
 * which is expected to be in a key/value form. If tighter control is required, you can use NotesModel
 * functions. If there are no subnotes for a position, the compound note will be removed. If a subnote
 * is set on a position where there is no compound note, one will be created for you.
 */
class PatternModel : public NotesModel
{
    Q_OBJECT
    /**
     * \brief The length of each row in the model (similar to column count, but for all rows)
     * @note Setting this to a value smaller than the current state will remove any notes set in the overflow columns
     * @default 16
     */
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY widthChanged)
    /**
     * \brief The amount of rows in the model (similar to rows, but actively enforced)
     * Active enforcement means that any change outside the given size will cause that change to be aborted
     * @note Setting this to a value smaller than the current state will remove any notes set in the overflow rows)
     * @default 1
     */
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
public:
    explicit PatternModel(PlayGridManager* parent = nullptr);
    ~PatternModel() override;

    /**
     * \brief Replace all notes in the model with ones with the same note value, but the given midiChannel instead
     * @param midiChannel The new midi channel to be set for all notes in the model
     */
    Q_INVOKABLE void setMidiChannel(int midiChannel);

    /**
     * \brief Add a new entry to the position
     * @param row The row you wish to add a new entry in
     * @param column The column in that row you wish to add a new entry into
     * @param note The note you wish to add to this position
     * @return The subnote position of the newly added note (for convenience with e.g. setEntryMetadata)
     */
    int addSubnote(int row, int column, QObject* note);

    /**
     * \brief Remove the entry at the given position in the model
     * @param row The row you wish to look in
     * @param column The column you wish to look at in that row
     * @param subnote The specific entry in that location's list of values that you wish to remove
     */
    void removeSubnote(int row, int column, int subnote);

    /**
     * \brief Set the specified metadata key to the given value for the given position
     * @param row The row you wish to look int
     * @param column The column in the given row you wish to look in
     * @param subnote The specific entry in that location's list of values that you wish to set metadata for
     * @param key The name of the specific metadata you wish to set the value for
     * @param value The new value you wish to set for the given key (pass an invalid variant to remove the key from the list)
     */
    void setSubnoteMetadata(int row, int column, int subnote, const QString &key, const QVariant &value);

    /**
     * \brief Get the metadata value for the specified key at the given position in the model
     * @param row The row you wish to look in
     * @param column The column in the given row you wish to look in
     * @param subnote The specific entry in that location's list of values that you wish to retrieve metadata from
     * @param key The key of the metadata you wish to fetch the value for. Pass an empty string to be given the entire hash
     * @return The requested metadata (or an invalid variant if none was found)
     */
    QVariant subnoteMetadata(int row, int column, int subnote, const QString &key);

    int width() const;
    void setWidth(int width);
    Q_SIGNAL void widthChanged();

    int height() const;
    void setHeight(int height);
    Q_SIGNAL void heightChanged();
private:
    class Private;
    Private *d;
};

#endif//PATTERNMODEL_H
