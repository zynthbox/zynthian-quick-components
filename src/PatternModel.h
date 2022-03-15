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
     * \brief The SequenceModel this PatternModel instance belongs to
     */
    Q_PROPERTY(QObject* sequence READ sequence CONSTANT)
    /**
     * \brief A human-readable name for this pattern (removes the parent sequence's name from the objectName if one is set)
     */
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    /**
     * \brief The destination for notes in this pattern (currently either synth or sample)
     * This controls whether this pattern fires notes into the midi world, or whether it uses
     * the pattern to control samples being fired.
     * @default PatternModel::NoteDestination::SynthDestination
     */
    Q_PROPERTY(PatternModel::NoteDestination noteDestination READ noteDestination WRITE setNoteDestination NOTIFY noteDestinationChanged)
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
     * @note Setting the midi channel to 15 will cause no notes to be played (as this is considered a control channel and not for notes)
     * @default 15
     */
    Q_PROPERTY(int midiChannel READ midiChannel WRITE setMidiChannel NOTIFY midiChannelChanged)
    /**
     * \brief The layer associated with this pattern
     * @note This is currently an alias of the midiChannel property, but this will likely change (so that midiChannel becomes
     *       more like a convenience alias for the layer's midiChannel property, but for now...)
     */
    Q_PROPERTY(int layer READ midiChannel WRITE setMidiChannel NOTIFY midiChannelChanged)
    /*
     * \brief A JSON representation of the sound associated with this pattern
     * @note Technically this does not get used by the pattern itself, but it is stored along with all the other data that
     *       makes up the pattern, so that if it is shared, it can be consumed by the importer and applied to its new home.
     */
    Q_PROPERTY(QString layerData READ layerData WRITE setLayerData NOTIFY layerDataChanged)
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
     * \brief The alphabetical name of the current bank (an upper case A or B, for example)
     * @default A
     * @see bankOffset
     * @see bankLength
     */
    Q_PROPERTY(QString bank READ bank WRITE setBank NOTIFY bankOffsetChanged)
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
    /**
     * \brief The IDs of the clips being used for the sample trigger and slice note destination modes
     */
    Q_PROPERTY(QVariantList clipIds READ clipIds WRITE setClipIds NOTIFY clipIdsChanged)
    /**
     * \brief The row which most recently had a note scheduled to be played
     * @default 0
     */
    Q_PROPERTY(int playingRow READ playingRow NOTIFY playingRowChanged)
    /**
     * \brief The column which most recently had a note scheduled to be played
     * @default 0
     */
    Q_PROPERTY(int playingColumn READ playingColumn NOTIFY playingColumnChanged)
    /**
     * \brief The global playback position within the pattern
     * This property will be -1 when the Pattern is not being played back
     * If played back, it will be ((playingRow * width) + playingColumn)
     * When using this for displaying a position in the UI, remember to also check the bank
     * to see whether what you are displaying should display the position. You can subtract
     * (bankOffset * width) to find a local position inside your current bank, or you can use
     * bankPlaybackPosition to get this value (though you should ensure to also check whether
     * the bank you are displaying is the one which is currently selected)
     */
    Q_PROPERTY(int playbackPosition READ playbackPosition NOTIFY playingColumnChanged)
    /**
     * \brief The bank-local playback position (see also playbackPosition)
     * This property will contain the value of playbackPosition, but with the subtraction of
     * the bank offset already done for you.
     * @note When using this for displaying positions, make sure to check you are displaying
     * the currently selected bank.
     */
    Q_PROPERTY(int bankPlaybackPosition READ bankPlaybackPosition NOTIFY playingColumnChanged)
    /**
     * \brief Whether or not this pattern is currently included in playback
     * This is essentially the same as performing a check on the parent sequence to see whether
     * that is playing, and then further checking whether this pattern is the current solo track
     * if one is set, and if none is set then whether the pattern is enabled.
     */
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)

    /**
     * \brief The first note used to fill out the grid model
     * @default 48
     */
    Q_PROPERTY(int gridModelStartNote READ gridModelStartNote WRITE setGridModelStartNote NOTIFY gridModelStartNoteChanged)
    /**
     * \brief The last note used to fill out the grid model
     * @default 64
     */
    Q_PROPERTY(int gridModelEndNote READ gridModelEndNote WRITE setGridModelEndNote NOTIFY gridModelEndNoteChanged)
    /**
     * \brief A NotesModel instance which shows a set of rows of notes based on the start and end note properties (in a comfortable spread)
     */
    Q_PROPERTY(QObject* gridModel READ gridModel CONSTANT)
    /**
     * \brief A NotesModel instance which shows appropriate entries for the slices in the clips associated with this pattern
     */
    Q_PROPERTY(QObject* clipSliceNotes READ clipSliceNotes CONSTANT)
public:
    explicit PatternModel(SequenceModel* parent = nullptr);
    ~PatternModel() override;

    enum NoteDestination {
        SynthDestination = 0,
        SampleTriggerDestination = 1,
        SampleLoopedDestination = 2,
        SampleSlicedDestination = 3,
    };
    Q_ENUM(NoteDestination)

    /**
     * \brief Clear this pattern and replace all contents and settings with those contained in the given pattern
     * @param otherPattern The pattern whose details you want to clone into this one
     */
    Q_INVOKABLE void cloneOther(PatternModel *otherPattern);

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
     * \brief Set the indicated position in the model to the given note
     * @note This function (and setMetadata) is vital and if you must change notes in ways that are not covered in other
     * PatternModel functions, use this, not the ones on NotesModel, otherwise playback will not work correctly!
     * This sets a specified location to contain the Note object passed to the function. If the location does
     * not yet exist, rows will be appended to the model until there are that many rows, and column added to
     * the row until the position exists. Clearing the position will not remove the position from the model.
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to set to the given note
     * @param column The column of the position to set to the given note
     * @param note The new note to be set in the specified location (this may be null, to clear the position)
     */
    Q_INVOKABLE void setNote(int row, int column, QObject *note) override;

    /**
     * \brief Set an abstract piece of metadata for the given position
     * @note This function (and setNote) is vital and if you must change metadata in ways that are not covered in other
     * PatternModel functions, use this, not the ones on NotesModel, otherwise playback will not work correctly!
     * @note Not valid on child models (see parentModel())
     * @param row The row of the position to set metadata for
     * @param column The column of the position to set the metadata for
     * @param metadata The piece of metadata you wish to set
     */
    Q_INVOKABLE void setMetadata(int row, int column, QVariant metadata) override;

    /**
     * \brief Removes all notes and metadata from the model
     */
    Q_INVOKABLE void clear() override;

    /**
     * \brief Removes all notes and metadata from the given row (if it exists)
     * @param row The row that you wish to clear of all data
     */
    Q_INVOKABLE void clearRow(int row);

    /**
     * \brief Clear all the rows in the given bank
     * @param bank The index of the bank (being the bank starting at the row bank * bankLength)
     */
    Q_INVOKABLE void clearBank(int bank);

    /**
     * \brief This will export a json representation of the pattern to a file with the given filename
     * @note This will overwrite anything that already exists in that location without warning
     * @param fileName The file you wish to write the pattern's json representation to
     * @return True if the file was successfully written, otherwise false
     */
    Q_INVOKABLE bool exportToFile(const QString &fileName) const;

    QObject* sequence() const;

    QString name() const;
    Q_SIGNAL void nameChanged();

    PatternModel::NoteDestination noteDestination() const;
    void setNoteDestination(const PatternModel::NoteDestination &noteDestination);
    Q_SIGNAL void noteDestinationChanged();

    int width() const;
    void setWidth(int width);
    Q_SIGNAL void widthChanged();

    int height() const;
    void setHeight(int height);
    Q_SIGNAL void heightChanged();

    void setMidiChannel(int midiChannel);
    int midiChannel() const;
    Q_SIGNAL void midiChannelChanged();

    void setLayerData(const QString &layerData);
    QString layerData() const;
    Q_SIGNAL void layerDataChanged();

    void setNoteLength(int noteLength);
    int noteLength() const;
    Q_SIGNAL void noteLengthChanged();

    void setAvailableBars(int availableBars);
    int availableBars() const;
    Q_SIGNAL void availableBarsChanged();

    void setActiveBar(int activeBar);
    int activeBar() const;
    Q_SIGNAL void activeBarChanged();

    void setBank(const QString& bank);
    QString bank() const;

    void setBankOffset(int bankOffset);
    int bankOffset() const;
    Q_SIGNAL void bankOffsetChanged();

    void setBankLength(int bankLength);
    int bankLength() const;
    Q_SIGNAL void bankLengthChanged();

    /**
     * \brief Whether the given bank contains any notes at all
     * In QML, you can "bind" to this by using the trick that the lastModified property changes. For example,
     * you might do something like:
     * <code>
       enabled: pattern.lastModified > -1 ? pattern.bankHasNotes(bankIndex) : pattern.bankHasNotes(bankIndex)
     * </code>
     * which will update the enabled property when lastModified changes, and also prefill it on the first run.
     * @param bankIndex The index of the bank to check for notes
     * @return True if the bank at the given index contains any notes
     */
    Q_INVOKABLE bool bankHasNotes(int bankIndex);

    void setEnabled(bool enabled);
    bool enabled() const;
    Q_SIGNAL void enabledChanged();

    void setClipIds(const QVariantList &ids);
    QVariantList clipIds() const;
    Q_SIGNAL void clipIdsChanged();
    QObject *clipSliceNotes() const;

    int gridModelStartNote() const;
    void setGridModelStartNote(int gridModelStartNote);
    Q_SIGNAL void gridModelStartNoteChanged();
    int gridModelEndNote() const;
    void setGridModelEndNote(int gridModelEndNote);
    Q_SIGNAL void gridModelEndNoteChanged();
    QObject *gridModel() const;

    int playingRow() const;
    Q_SIGNAL void playingRowChanged();
    int playingColumn() const;
    Q_SIGNAL void playingColumnChanged();
    int playbackPosition() const;
    int bankPlaybackPosition() const;

    bool isPlaying() const;
    Q_SIGNAL void isPlayingChanged();

    Q_INVOKABLE void setPositionOff(int row, int column) const;
    Q_INVOKABLE QObjectList setPositionOn(int row, int column) const;

    /**
     * \brief Used by SequenceModel to advance the sequence position during playback
     *
     * Schedules notes to be set on and off depending on the sequence position and the note length
     * of this Pattern (notes will be scheduled for on/off on the beat preceding their location in
     * the Pattern, to ensure the lowest possible latency)
     * @param sequencePosition The position in the sequence that should be considered (literally a count of ticks)
     * @param progressionLength The number of ticks until the next position (that is, how many ticks between this and the next call of the function)
     */
    void handleSequenceAdvancement(quint64 sequencePosition, int progressionLength) const;
    /**
     * \brief Used by SequenceModel to update its patterns' positions to the actual sequence playback position during playback
     *
     * @param sequencePosition The position in the sequence that should be considered
     */
    void updateSequencePosition(quint64 sequencePosition);
    /**
     * \brief When turning off playback, this function will turn off any notes that are waiting to be turned off
     */
    void handleSequenceStop();

    Q_SLOT void handleMidiMessage(const unsigned char &byte1, const unsigned char &byte2, const unsigned char &byte3);
private:
    class Private;
    Private *d;
};
Q_DECLARE_METATYPE(PatternModel::NoteDestination)

#endif//PATTERNMODEL_H
