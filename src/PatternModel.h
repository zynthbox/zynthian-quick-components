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
#include "SequenceModel.h"

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
     * @default 16
     */
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    /**
     * \brief The midi channel used by all notes in this pattern
     * This is potentially an expensive operation, as it will replace all notes in the model with ones matching the newly set channel
     * @note When adding and removing notes to the model, they will be checked (and changed to fit)
     * The range for this property is 0-15 (and any attempt to set it outside of that value will be clamped)
     * @default 0
     */
    Q_PROPERTY(int midiChannel READ midiChannel WRITE setMidiChannel NOTIFY midiChannelChanged)
    /**
     * \brief The layer associated with this pattern
     * @note This is currently an alias of the midiChannel property, but this will likely change (so that midiChannel becomes
     *       more like a convenience alias for the layer's midiChannel property, but for now...)
     */
    Q_PROPERTY(int layer READ midiChannel WRITE setMidiChannel NOTIFY midiChannelChanged)
    /**
     * \brief The duration of a note in the pattern (the subdivision used to determine the speed of playback)
     * Values from 1 through 6, which each translate to the following:
     * 1: quarter
     * 2: half
     * 3: normal
     * 4: double
     * 5: quadruple
     * 6: octuple
     * @default 3
     */
    Q_PROPERTY(int noteLength READ noteLength WRITE setNoteLength NOTIFY noteLengthChanged)
    /**
     * \brief The number of bars in the pattern which should be considered for playback
     * @default 1
     */
    Q_PROPERTY(int availableBars READ availableBars WRITE setAvailableBars NOTIFY availableBarsChanged)
    /**
     * \brief Which bar (row) should be considered current
     * This will be clamped to the available range (the lowest value is 0, maximum is height-1)
     * @default 0
     */
    Q_PROPERTY(int activeBar READ activeBar WRITE setActiveBar NOTIFY activeBarChanged)
    /**
     * \brief An offset used to display a subsection of rows (a bank)
     * Default value is 0
     * @see bankLength
     */
    Q_PROPERTY(int bankOffset READ bankOffset WRITE setBankOffset NOTIFY bankOffsetChanged)
    /**
     * \brief The length of a bank (a subset of rows)
     * Default value is 8
     */
    Q_PROPERTY(int bankLength READ bankLength WRITE setBankLength NOTIFY bankLengthChanged)
    /**
     * \brief A toggle for setting the pattern to an enabled state (primarily used for playback purposes)
     * @default true
     */
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit PatternModel(SequenceModel* parent = nullptr);
    ~PatternModel() override;

    /**
     * \brief The subnote position of the note with the given midi note value in the requested position in the model
     * @param row The row you wish to check in
     * @param column The column in the row you wish to check in
     * @param midiNote The note value you wish to check
     * @return The index of the subnote, or -1 if not found
     */
    Q_INVOKABLE int subnoteIndex(int row, int column, int midiNote) const;
    /**
     * \brief Add a new entry to the position
     * @param row The row you wish to add a new entry in
     * @param column The column in that row you wish to add a new entry into
     * @param note The note you wish to add to this position
     * @return The subnote position of the newly added note (for convenience with e.g. setEntryMetadata)
     */
    Q_INVOKABLE int addSubnote(int row, int column, QObject* note);

    /**
     * \brief Remove the entry at the given position in the model
     * @param row The row you wish to look in
     * @param column The column you wish to look at in that row
     * @param subnote The specific entry in that location's list of values that you wish to remove
     */
    Q_INVOKABLE void removeSubnote(int row, int column, int subnote);

    /**
     * \brief Set the specified metadata key to the given value for the given position
     * @param row The row you wish to look int
     * @param column The column in the given row you wish to look in
     * @param subnote The specific entry in that location's list of values that you wish to set metadata for
     * @param key The name of the specific metadata you wish to set the value for
     * @param value The new value you wish to set for the given key (pass an invalid variant to remove the key from the list)
     */
    Q_INVOKABLE void setSubnoteMetadata(int row, int column, int subnote, const QString &key, const QVariant &value);

    /**
     * \brief Get the metadata value for the specified key at the given position in the model
     * @param row The row you wish to look in
     * @param column The column in the given row you wish to look in
     * @param subnote The specific entry in that location's list of values that you wish to retrieve metadata from
     * @param key The key of the metadata you wish to fetch the value for. Pass an empty string to be given the entire hash
     * @return The requested metadata (or an invalid variant if none was found)
     */
    Q_INVOKABLE QVariant subnoteMetadata(int row, int column, int subnote, const QString &key);

    /**
     * \brief Removes all notes and metadata from the model
     */
    Q_INVOKABLE void clear() override;

    /**
     * \brief Removes all notes and metadata from the given row (if it exists)
     * @param row The row that you wish to clear of all data
     */
    Q_INVOKABLE void clearRow(int row);

    int width() const;
    void setWidth(int width);
    Q_SIGNAL void widthChanged();

    int height() const;
    void setHeight(int height);
    Q_SIGNAL void heightChanged();

    void setMidiChannel(int midiChannel);
    int midiChannel() const;
    Q_SIGNAL void midiChannelChanged();

    void setNoteLength(int noteLength);
    int noteLength() const;
    Q_SIGNAL void noteLengthChanged();

    void setAvailableBars(int availableBars);
    int availableBars() const;
    Q_SIGNAL void availableBarsChanged();

    void setActiveBar(int activeBar);
    int activeBar() const;
    Q_SIGNAL void activeBarChanged();

    void setBankOffset(int bankOffset);
    int bankOffset() const;
    Q_SIGNAL void bankOffsetChanged();

    void setBankLength(int bankLength);
    int bankLength() const;
    Q_SIGNAL void bankLengthChanged();

    void setEnabled(bool enabled);
    bool enabled() const;
    Q_SIGNAL void enabledChanged();

    Q_INVOKABLE void setPositionOff(int row, int column) const;
    Q_INVOKABLE QObjectList setPositionOn(int row, int column) const;
private:
    class Private;
    Private *d;
};

#endif//PATTERNMODEL_H
