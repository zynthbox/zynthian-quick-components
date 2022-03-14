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

#include "PatternModel.h"
#include "Note.h"

#include <QDebug>
#include <QFile>
#include <QPointer>
#include <QTimer>

// Hackety hack - we don't need all the thing, just need some storage things (MidiBuffer and MidiNote specifically)
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1
#include <juce_audio_formats/juce_audio_formats.h>

#include <libzl.h>
#include <ClipCommand.h>
#include <ClipAudioSource.h>
#include <SamplerSynth.h>
#include <SyncTimer.h>

#define CLIP_COUNT 5

class PatternModel::Private {
public:
    Private() {
        playGridManager = PlayGridManager::instance();
        samplerSynth = SamplerSynth::instance();
        for (int i = 0; i < CLIP_COUNT; ++i) {
            clips << nullptr;
        }
    }
    ~Private() {}
    int width{16};
    PatternModel::NoteDestination noteDestination{PatternModel::SynthDestination};
    int midiChannel{15};
    QString layerData;
    int noteLength{3};
    int availableBars{1};
    int activeBar{0};
    int bankOffset{0};
    int bankLength{8};
    bool enabled{true};
    int playingRow{0};
    int playingColumn{0};

    // These two are equivalent to the data found in each note, and is stored
    // per-position (the index is row * width + column). These must be cleared
    // on any change of the notes (which should always be done through setNote
    // and setMetadata to ensure this). If they are not cleared on changes, what
    // ends up sent to SyncTimer during playback will not match what the model
    // contains. So, remember your pattern hygiene and clean your buffers!
    QHash<int, juce::MidiBuffer> onBuffers;
    QHash<int, juce::MidiBuffer> offBuffers;
    SyncTimer* syncTimer{nullptr};
    SequenceModel *sequence;

    PlayGridManager *playGridManager{nullptr};
    SamplerSynth *samplerSynth{nullptr};

    int gridModelStartNote{48};
    int gridModelEndNote{64};
    NotesModel *gridModel{nullptr};
    NotesModel *clipSliceNotes{nullptr};
    QList< QPointer<ClipAudioSource> > clips;
    /**
     * This function will return the last clip in the list which has a
     * sliceBaseMidiNote higher or equal to the given midi note
     * @param midiNote The midi note to find a clip for
     * @return The clip audio source that best matches the given midi note (or nullptr if the list is empty)
     */
    ClipAudioSource *clipForMidiNote(int midiNote) {
        ClipAudioSource *clip{nullptr};
        for (int i = CLIP_COUNT - 1; i > -1; --i) {
            ClipAudioSource *needle = clips[i];
            if (needle && needle->sliceBaseMidiNote() >= midiNote) {
                clip = needle;
                break;
            }
        }
        // Otherwise, get the first and best clip and use that
        if (!clip) {
            for (const QPointer<ClipAudioSource> &needle : clips) {
                if (needle) {
                    clip = needle;
                }
            }
        }
        return clip;
    }
};

PatternModel::PatternModel(SequenceModel* parent)
    : NotesModel(parent ? parent->playGridManager() : nullptr)
    , d(new Private)
{
    // We need to make sure that we support orphaned patterns (that is, a pattern that is not contained within a sequence)
    d->sequence = parent;
    if (parent) {
        connect(d->sequence, &SequenceModel::isPlayingChanged, this, &PatternModel::isPlayingChanged);
        connect(d->sequence, &SequenceModel::soloPatternChanged, this, &PatternModel::isPlayingChanged);
        connect(this, &PatternModel::enabledChanged, this, &PatternModel::isPlayingChanged);
        // This is to ensure that when the current sound changes and we have no midi channel, we will schedule
        // the notes that are expected of us
        connect(d->sequence->playGridManager(), &PlayGridManager::currentMidiChannelChanged, this, [this](){
            if (d->midiChannel == 15 && d->sequence->playGridManager()->currentMidiChannel() > -1) {
                d->onBuffers.clear();
                d->offBuffers.clear();
            }
        });
    }
    // This will force the creation of a whole bunch of rows with the desired width and whatnot...
    setHeight(16);

    connect(this, &QObject::objectNameChanged, this, &PatternModel::nameChanged);
    static const int noteDestinationTypeId = qRegisterMetaType<NoteDestination>();
    Q_UNUSED(noteDestinationTypeId)

    connect(d->playGridManager, &PlayGridManager::mostRecentlyChangedNotesChanged, this, &PatternModel::handleNotesChanging, Qt::DirectConnection);
}

PatternModel::~PatternModel()
{
    delete d;
}

void PatternModel::cloneOther(PatternModel *otherPattern)
{
    if (otherPattern) {
        clear();
        setWidth(otherPattern->width());
        setHeight(otherPattern->height());
        setMidiChannel(otherPattern->midiChannel());
        setLayerData(otherPattern->layerData());
        setNoteLength(otherPattern->noteLength());
        setAvailableBars(otherPattern->availableBars());
        setActiveBar(otherPattern->activeBar());
        setBankOffset(otherPattern->bankOffset());
        setBankLength(otherPattern->bankLength());
        setEnabled(otherPattern->enabled());

        // Now clone all the notes
        for (int i = 0; i < rowCount(); ++i) {
            setRowData(i, otherPattern->getRow(i), otherPattern->getRowMetadata(i));
        }
    }
}

int PatternModel::subnoteIndex(int row, int column, int midiNote) const
{
    int result{-1};
    if (row > -1 && row < height() && column > -1 && column < width()) {
        const Note* note = qobject_cast<Note*>(getNote(row, column));
        if (note) {
            for (int i = 0; i < note->subnotes().count(); ++i) {
                const Note* subnote = note->subnotes().at(i).value<Note*>();
                if (subnote && subnote->midiNote() == midiNote) {
                    result = i;
                    break;
                }
            }
        }
    }
    return result;
}

int PatternModel::addSubnote(int row, int column, QObject* note)
{
    int newPosition{-1};
    if (row > -1 && row < height() && column > -1 && column < width() && note) {
        Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
        QVariantList subnotes;
        QVariantList metadata;
        if (oldCompound) {
            subnotes = oldCompound->subnotes();
            metadata = getMetadata(row, column).toList();
        }
        newPosition = subnotes.count();

        // Ensure the note is correct according to our midi channel settings
        Note *newNote = qobject_cast<Note*>(note);
        if (newNote->midiChannel() != d->midiChannel) {
            newNote = qobject_cast<Note*>(playGridManager()->getNote(newNote->midiNote(), d->midiChannel));
        }
        subnotes.append(QVariant::fromValue<QObject*>(newNote));

        metadata.append(QVariantHash());
        setNote(row, column, playGridManager()->getCompoundNote(subnotes));
        setMetadata(row, column, metadata);
    }
    return newPosition;
}

void PatternModel::removeSubnote(int row, int column, int subnote)
{
    if (row > -1 && row < height() && column > -1 && column < width()) {
        Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
        QVariantList subnotes;
        QVariantList metadata;
        if (oldCompound) {
            subnotes = oldCompound->subnotes();
            metadata = getMetadata(row, column).toList();
        }
        if (subnote > -1 && subnote < subnotes.count()) {
            subnotes.removeAt(subnote);
            metadata.removeAt(subnote);
        }
        setNote(row, column, playGridManager()->getCompoundNote(subnotes));
        setMetadata(row, column, metadata);
    }
}

void PatternModel::setSubnoteMetadata(int row, int column, int subnote, const QString& key, const QVariant& value)
{
    if (row > -1 && row < height() && column > -1 && column < width()) {
        const QVariant rawMeta(getMetadata(row, column).toList());
        QVariantList metadata;
        if (rawMeta.isValid() && rawMeta.canConvert<QVariantList>()) {
            metadata = rawMeta.toList();
        } else {
            Note *note = qobject_cast<Note*>(getNote(row, column));
            if (note) {
                for (int i = 0; i < note->subnotes().count(); ++i) {
                    metadata << QVariantHash();
                }
            }
        }
        if (subnote > -1 && subnote < metadata.count()) {
            QVariantHash noteMetadata = metadata.at(subnote).toHash();
            if (value.isValid()) {
                noteMetadata[key] = value;
            } else {
                noteMetadata.remove(key);
            }
            metadata[subnote] = noteMetadata;
        }
        setMetadata(row, column, metadata);
    }
}

QVariant PatternModel::subnoteMetadata(int row, int column, int subnote, const QString& key)
{
    QVariant result;
    if (row > -1 && row < height() && column > -1 && column < width()) {
        const QVariantList metadata = getMetadata(row, column).toList();
        if (subnote > -1 && subnote < metadata.count()) {
            result.setValue(metadata.at(subnote).toHash().value(key));
        }
    }
    return result;
}

void PatternModel::setNote(int row, int column, QObject* note)
{
    d->onBuffers.remove((row * d->width) + column);
    d->offBuffers.remove((row * d->width) + column);
    NotesModel::setNote(row, column, note);
}

void PatternModel::setMetadata(int row, int column, QVariant metadata)
{
    d->onBuffers.remove((row * d->width) + column);
    d->offBuffers.remove((row * d->width) + column);
    NotesModel::setMetadata(row, column, metadata);
}

void PatternModel::clear()
{
    beginResetModel();
    for (int row = 0; row < rowCount(); ++row) {
        clearRow(row);
    }
    endResetModel();
}

void PatternModel::clearRow(int row)
{
    for (int column = 0; column < d->width; ++column) {
        setNote(row, column, nullptr);
        setMetadata(row, column, QVariantList());
    }
}

void PatternModel::clearBank(int bank)
{
    for (int i = 0; i < bankLength(); ++i) {
        clearRow((bankLength() * bank) + i);
    }
}

void PatternModel::setWidth(int width)
{
    if (this->width() < width) {
        // Force these to exist if wider than current
        for (int row = 0; row < height(); ++row) {
            setNote(row, width - 1, nullptr);
        }
    } else {
        // Remove any that are superfluous if narrower
        for (int row = 0; row < height(); ++row) {
            QVariantList rowNotes(getRow(row));
            QVariantList rowMetadata(getRowMetadata(row));
            while (rowNotes.count() > width) {
                rowNotes.removeAt(rowNotes.count() - 1);
                rowMetadata.removeAt(rowNotes.count() - 1);
            }
            setRowData(row, rowNotes, rowMetadata);
        }
    }
}

bool PatternModel::exportToFile(const QString &fileName) const
{
    bool success{false};
    QFile patternFile(fileName);
    if (patternFile.open(QIODevice::WriteOnly)) {
        patternFile.write(playGridManager()->modelToJson(this).toUtf8());
        patternFile.close();
        success = true;
    }
    return success;
}

QObject* PatternModel::sequence() const
{
    return d->sequence;
}

QString PatternModel::name() const
{
    // To ensure we can have orphaned models, we can't assume an associated sequence
    int parentNameLength{0};
    if (d->sequence) {
        parentNameLength = d->sequence->objectName().length();
    }
    return objectName().left(objectName().length() - (parentNameLength + 3));
}

PatternModel::NoteDestination PatternModel::noteDestination() const
{
    return d->noteDestination;
}

void PatternModel::setNoteDestination(const PatternModel::NoteDestination &noteDestination)
{
    if (d->noteDestination != noteDestination) {
        d->noteDestination = noteDestination;
        Q_EMIT noteDestinationChanged();
    }
}

int PatternModel::width() const
{
    return d->width;
}

void PatternModel::setHeight(int height)
{
    if (this->height() < height) {
        // Force these to exist if taller than current
        for (int i = this->height(); i < height; ++i) {
            setNote(i, width() - 1, nullptr);
        }
    } else {
        // Remove any that are superfluous if shorter
        while (this->height() > height) {
            removeRow(this->height() - 1);
        }
    }
}

int PatternModel::height() const
{
    return rowCount();
}

void PatternModel::setMidiChannel(int midiChannel)
{
    int actualChannel = qMin(qMax(-1, midiChannel), 15);
    if (d->midiChannel != actualChannel) {
        d->midiChannel = actualChannel;
        for (int row = 0; row < rowCount(); ++row) {
            for (int column = 0; column < columnCount(createIndex(row, 0)); ++column) {
                Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
                QVariantList newSubnotes;
                if (oldCompound) {
                    for (const QVariant &subnote :oldCompound->subnotes()) {
                        Note *oldNote = subnote.value<Note*>();
                        if (oldNote) {
                            newSubnotes << QVariant::fromValue<QObject*>(playGridManager()->getNote(oldNote->midiNote(), actualChannel));
                        } else {
                            // This really shouldn't happen - spit out a warning and slap in something unknown so we keep the order intact
                            newSubnotes << QVariant::fromValue<QObject*>(playGridManager()->getNote(0, actualChannel));
                            qWarning() << "Failed to convert a subnote value which must be a Note object to a Note object - something clearly isn't right.";
                        }
                    }
                }
                setNote(row, column, playGridManager()->getCompoundNote(newSubnotes));
            }
        }
        d->onBuffers.clear();
        d->offBuffers.clear();
        Q_EMIT midiChannelChanged();
    }
}

int PatternModel::midiChannel() const
{
    return d->midiChannel;
}

void PatternModel::setLayerData(const QString &layerData)
{
    if (d->layerData != layerData) {
        d->layerData = layerData;
        Q_EMIT layerDataChanged();
    }
}

QString PatternModel::layerData() const
{
    return d->layerData;
}

void PatternModel::setNoteLength(int noteLength)
{
    if (d->noteLength != noteLength) {
        d->noteLength = noteLength;
        d->onBuffers.clear();
        d->offBuffers.clear();
        Q_EMIT noteLengthChanged();
    }
}

int PatternModel::noteLength() const
{
    return d->noteLength;
}

void PatternModel::setAvailableBars(int availableBars)
{
    int adjusted = qMin(qMax(0, availableBars), height());
    if (d->availableBars != adjusted) {
        d->availableBars = adjusted;
        Q_EMIT availableBarsChanged();
    }
}

int PatternModel::availableBars() const
{
    return d->availableBars;
}

void PatternModel::setActiveBar(int activeBar)
{
    if (d->activeBar != activeBar) {
        d->activeBar = activeBar;
        Q_EMIT activeBarChanged();
    }
}

int PatternModel::activeBar() const
{
    return d->activeBar;
}

void PatternModel::setBank(const QString& bank)
{
    // A, B, and C are some old fallback stuff...
    int newOffset{d->bankOffset};
    if (bank.toUpper() == "A" || bank.toUpper() == "I") {
        newOffset = 0;
    } else if (bank.toUpper() == "B" || bank.toUpper() == "II") {
        newOffset = d->bankLength;
    } else if (bank.toUpper() == "C" || bank.toUpper() == "III") {
        newOffset = d->bankLength * 2;
    }
    setBankOffset(newOffset);
}

QString PatternModel::bank() const
{
    static const QStringList names{QLatin1String{"I"}, QLatin1String{"II"}, QLatin1String{"III"}};
    int bankNumber{d->bankOffset / d->bankLength};
    QString result{"(?)"};
    if (bankNumber < names.count()) {
        result = names[bankNumber];
    }
    return result;
}

void PatternModel::setBankOffset(int bankOffset)
{
    if (d->bankOffset != bankOffset) {
        d->bankOffset = bankOffset;
        Q_EMIT bankOffsetChanged();
    }
}

int PatternModel::bankOffset() const
{
    return d->bankOffset;
}

void PatternModel::setBankLength(int bankLength)
{
    if (d->bankLength != bankLength) {
        d->bankLength = bankLength;
        Q_EMIT bankLengthChanged();
    }
}

int PatternModel::bankLength() const
{
    return d->bankLength;
}

bool PatternModel::bankHasNotes(int bankIndex)
{
    bool hasNotes{false};
    for (int row = 0; row < d->bankLength; ++row) {
        for (int column = 0; column < d->width; ++column) {
            Note* note = qobject_cast<Note*>(getNote(row + (bankIndex * d->bankLength), column));
            if (note && note->subnotes().length() > 0) {
                hasNotes = true;
                break;
            }
        }
        if (hasNotes) {
            break;
        }
    }
    return hasNotes;
}

void PatternModel::setEnabled(bool enabled)
{
    if (d->enabled != enabled) {
        d->enabled = enabled;
        Q_EMIT enabledChanged();
    }
}

bool PatternModel::enabled() const
{
    return d->enabled;
}

void PatternModel::setClipIds(const QVariantList &clipIds)
{
    bool changed{false};
    int i{0};
    for (const QVariant &clipId : clipIds) {
        ClipAudioSource *newClip = ClipAudioSource_byID(clipId.toInt());
        if (d->clips[i] != newClip) {
            d->clips[i] = newClip;
            changed = true;
        }
        ++i;
    }
    if (changed) {
        Q_EMIT clipIdsChanged();
    }
}

QVariantList PatternModel::clipIds() const
{
    QVariantList ids;
    for (int i = 0; i < CLIP_COUNT; ++i) {
        ClipAudioSource *clip = d->clips[i];
        if (clip) {
            ids << clip->id();
        } else {
            ids << -1;
        }
    }
    return ids;
}

QObject *PatternModel::clipSliceNotes() const
{
    if (!d->clipSliceNotes) {
        d->clipSliceNotes = qobject_cast<NotesModel*>(PlayGridManager::instance()->getNotesModel(objectName() + " - Clip Slice Notes Model"));
        auto fillClipSliceNotes = [this](){
            QList<int> notesToFit;
            QList<QString> noteTitles;
            ClipAudioSource *previousClip{nullptr};
            for (int i = 0; i < d->clips.count(); ++i) {
                ClipAudioSource *clip = d->clips[i];
                if (clip) {
                    int sliceStart{clip->sliceBaseMidiNote()};
                    int nextClipStart{129};
                    for (int j = i + 1; j < d->clips.count(); ++j) {
                        ClipAudioSource *nextClip = d->clips[j];
                        if (nextClip) {
                            nextClipStart = nextClip->sliceBaseMidiNote();
                            break;
                        }
                    }
                    // Let's see if we can push it /back/ a bit, and still get a full thing... less lovely, but it gives a full spread
                    if (nextClipStart - clip->slices() < sliceStart) {
                        sliceStart = qMax(previousClip ? previousClip->sliceBaseMidiNote() + previousClip->slices(): 0, nextClipStart - clip->slices());
                    }
                    // Now let's add as many notes as we need, or have space for, whichever is smaller
                    int addedNotes{0};
                    for (int note = sliceStart; note < nextClipStart && addedNotes < clip->slices(); ++note) {
                        notesToFit << note;
                        noteTitles << QString("Sample %1\nSlice %2").arg(QString::number(i + 1)).arg(QString::number(clip->sliceForMidiNote(note) + 1));
                        ++addedNotes;
                    }
                    previousClip = clip;
                }
            }
            int howManyRows{int(sqrt(notesToFit.length()))};
            int i{0};
            for (int row = 0; row < howManyRows; ++row) {
                QVariantList notes;
                QVariantList metadata;
                for (int column = 0; column < notesToFit.count() / howManyRows; ++column) {
                    if (i == notesToFit.count()) {
                        break;
                    }
                    notes << QVariant::fromValue<QObject*>(PlayGridManager::instance()->getNote(notesToFit[i], d->midiChannel));
                    metadata << QVariantMap{{"displayText", QVariant::fromValue<QString>(noteTitles[i])}};
                    ++i;
                }
                d->clipSliceNotes->appendRow(notes, metadata);
            }
        };
        connect(this, &PatternModel::clipIdsChanged, this, fillClipSliceNotes);
        connect(this, &PatternModel::midiChannelChanged, this, fillClipSliceNotes);
        fillClipSliceNotes();
    }
    return d->clipSliceNotes;
}

int PatternModel::gridModelStartNote() const
{
    return d->gridModelStartNote;
}

void PatternModel::setGridModelStartNote(int gridModelStartNote)
{
    if (d->gridModelStartNote != gridModelStartNote) {
        d->gridModelStartNote = gridModelStartNote;
        Q_EMIT gridModelStartNoteChanged();
    }
}

int PatternModel::gridModelEndNote() const
{
    return d->gridModelEndNote;
}

void PatternModel::setGridModelEndNote(int gridModelEndNote)
{
    if (d->gridModelEndNote != gridModelEndNote) {
        d->gridModelEndNote = gridModelEndNote;
        Q_EMIT gridModelEndNoteChanged();
    }
}

QObject *PatternModel::gridModel() const
{
    if (!d->gridModel) {
        d->gridModel = qobject_cast<NotesModel*>(PlayGridManager::instance()->getNotesModel(objectName() + " - Grid Model"));
        auto rebuildGridModel = [this](){
            qDebug() << "Rebuilding" << d->gridModel;
            QList<int> notesToFit;
            for (int note = d->gridModelStartNote; note <= d->gridModelEndNote; ++note) {
                notesToFit << note;
            }
            int howManyRows{int(sqrt(notesToFit.length()))};
            int i{0};
            d->gridModel->clear();
            for (int row = 0; row < howManyRows; ++row) {
                QVariantList notes;
                for (int column = 0; column < notesToFit.count() / howManyRows; ++column) {
                    if (i == notesToFit.count()) {
                        break;
                    }
                    notes << QVariant::fromValue<QObject*>(PlayGridManager::instance()->getNote(notesToFit[i], d->midiChannel));
                    ++i;
                }
                d->gridModel->addRow(notes);
            }
        };
        QTimer *refilTimer = new QTimer(d->gridModel);
        refilTimer->setInterval(1);
        refilTimer->setSingleShot(true);
        connect(refilTimer, &QTimer::timeout, d->gridModel, rebuildGridModel);
        connect(this, &PatternModel::midiChannelChanged, refilTimer, QOverload<>::of(&QTimer::start));
        connect(this, &PatternModel::gridModelStartNoteChanged, refilTimer, QOverload<>::of(&QTimer::start));
        connect(this, &PatternModel::gridModelEndNoteChanged, refilTimer, QOverload<>::of(&QTimer::start));
        rebuildGridModel();
    }
    return d->gridModel;
}

int PatternModel::playingRow() const
{
    return d->playingRow;
}

int PatternModel::playingColumn() const
{
    return d->playingColumn;
}

int PatternModel::playbackPosition() const
{
    return isPlaying()
        ? (d->playingRow * d->width) + d->playingColumn
        : -1;
}

int PatternModel::bankPlaybackPosition() const
{
    return isPlaying()
        ? (d->playingRow * d->width) + d->playingColumn - (d->bankOffset * d->width)
        : -1;
}

bool PatternModel::isPlaying() const
{
    bool isPlaying{false};
    if (d->sequence && d->sequence->isPlaying()) {
        if (d->sequence->soloPattern() > -1) {
            isPlaying = (d->sequence->soloPatternObject() == this);
        } else {
            isPlaying = d->enabled;
        }
    }
    return isPlaying;
}

void PatternModel::setPositionOff(int row, int column) const
{
    if (row > -1 && row < height() && column > -1 && column < width()) {
        const Note *note = qobject_cast<Note*>(getNote(row, column));
        if (note) {
            for (const QVariant &subnoteVar : note->subnotes()) {
                Note *subnote = subnoteVar.value<Note*>();
                if (subnote) {
                    subnote->setOff();
                }
            }
        }
    }
}

QObjectList PatternModel::setPositionOn(int row, int column) const
{
    static const QLatin1String velocityString{"velocity"};
    QObjectList onifiedNotes;
    if (row > -1 && row < height() && column > -1 && column < width()) {
        const Note *note = qobject_cast<Note*>(getNote(row, column));
        if (note) {
            const QVariantList &subnotes = note->subnotes();
            const QVariantList &meta = getMetadata(row, column).toList();
            if (meta.count() == subnotes.count()) {
                for (int i = 0; i < subnotes.count(); ++i) {
                    Note *subnote = subnotes[i].value<Note*>();
                    const QVariantHash &metaHash = meta[i].toHash();
                    if (metaHash.isEmpty() && subnote) {
                        playGridManager()->scheduleNote(subnote->midiNote(), subnote->midiChannel(), true);
                        onifiedNotes << subnote;
                    } else if (subnote) {
                        int velocity{64};
                        if (metaHash.contains(velocityString)) {
                            velocity = metaHash.value(velocityString).toInt();
                        }
                        playGridManager()->scheduleNote(subnote->midiNote(), subnote->midiChannel(), true, velocity);
                        onifiedNotes << subnote;
                    }
                }
            } else {
                for (const QVariant &subnoteVar : subnotes) {
                    Note *subnote = subnoteVar.value<Note*>();
                    if (subnote) {
                        playGridManager()->scheduleNote(subnote->midiNote(), subnote->midiChannel(), true);
                        onifiedNotes << subnote;
                    }
                }
            }
        }
    }
    return onifiedNotes;
}

void addNoteToBuffer(juce::MidiBuffer &buffer, const Note *theNote, unsigned char velocity, bool setOn, int overrideChannel) {
    if ((overrideChannel > -1 ? overrideChannel : theNote->midiChannel()) >= -1 && (overrideChannel > -1 ? overrideChannel : theNote->midiChannel()) <= 15) {
        unsigned char note[3];
        if (setOn) {
            note[0] = 0x90 + (overrideChannel > -1 ? overrideChannel : theNote->midiChannel());
        } else {
            note[0] = 0x80 + (overrideChannel > -1 ? overrideChannel : theNote->midiChannel());
        }
        note[1] = theNote->midiNote();
        note[2] = velocity;
        const int onOrOff = setOn ? 1 : 0;
        buffer.addEvent(note, 3, onOrOff);
    }
}

void PatternModel::handleSequenceAdvancement(quint64 sequencePosition, int progressionLength) const
{
    static const QLatin1String velocityString{"velocity"};
    // Don't play notes on channel 15, because that's the control channel, and we don't want patterns to play to that
    if (isPlaying() && ((d->midiChannel > -1 && d->midiChannel < 15) || d->playGridManager->currentMidiChannel() > -1)) {
        const int overrideChannel{(d->midiChannel == 15) ? d->playGridManager->currentMidiChannel() : -1};
        quint64 noteDuration{0};
        bool relevantToUs{false};
        // Since this happens at the /end/ of the cycle in a beat, this should be used to schedule beats for the next
        // beat, not the current one. That is to say, prepare the next frame, not the current one (since those notes
        // have already been played).
        for (int progressionIncrement = 1; progressionIncrement <= progressionLength; ++progressionIncrement) {
            // check whether the sequencePosition + progressionIncrement matches our note length
            quint64 nextPosition = sequencePosition + progressionIncrement;
            // Potentially it'd be tempting to try and optimise this manually to use bitwise operators,
            // but GCC already does that for you at -O2, so don't bother :)
            switch (d->noteLength) {
            case 1:
                if (nextPosition % 32 == 0) {
                    relevantToUs = true;
                    nextPosition = nextPosition / 32;
                    noteDuration = 32;
                } else {
                    relevantToUs = false;
                }
                break;
            case 2:
                if (nextPosition % 16 == 0) {
                    relevantToUs = true;
                    nextPosition = nextPosition / 16;
                    noteDuration = 16;
                } else {
                    relevantToUs = false;
                }
                break;
            case 3:
                if (nextPosition % 8 == 0) {
                    relevantToUs = true;
                    nextPosition = nextPosition / 8;
                    noteDuration = 8;
                } else {
                    relevantToUs = false;
                }
                break;
            case 4:
                if (nextPosition % 4 == 0) {
                    relevantToUs = true;
                    nextPosition = nextPosition / 4;
                    noteDuration = 4;
                } else {
                    relevantToUs = false;
                }
                break;
            case 5:
                if (nextPosition % 2 == 0) {
                    relevantToUs = true;
                    nextPosition = nextPosition / 2;
                    noteDuration = 2;
                } else {
                    relevantToUs = false;
                }
                break;
            case 6:
                relevantToUs = true;
                noteDuration = 1;
                break;
            default:
                qWarning() << "Incorrect note length in pattern, no notes will be played from this one, ever" << objectName();
                break;
            }

            if (relevantToUs) {
                // Get the next row/column combination, and schedule the previous one off, and the next one on
                // squish nextPosition down to fit inside our available range (d->availableBars * d->width)
                // start + (numberToBeWrapped - start) % (limit - start)
                nextPosition = nextPosition % (d->availableBars * d->width);
                int row = (nextPosition / d->width) % d->availableBars;
                int column = nextPosition - (row * d->width);

                if (!d->onBuffers.contains(nextPosition + (d->bankOffset * d->width))) {
                    juce::MidiBuffer onBuffer;
                    juce::MidiBuffer offBuffer;
                    const Note *note = qobject_cast<const Note*>(getNote(row + d->bankOffset, column));
                    if (note) {
                        const QVariantList &subnotes = note->subnotes();
                        const QVariantList &meta = getMetadata(row + d->bankOffset, column).toList();
                        if (meta.count() == subnotes.count()) {
                            for (int i = 0; i < subnotes.count(); ++i) {
                                const Note *subnote = subnotes[i].value<Note*>();
                                const QVariantHash &metaHash = meta[i].toHash();
                                if (subnote) {
                                    if (metaHash.isEmpty()) {
                                        addNoteToBuffer(onBuffer, subnote, 64, true, overrideChannel);
                                        addNoteToBuffer(offBuffer, subnote, 64, false, overrideChannel);
                                    } else {
                                        const int velocity{metaHash.value(velocityString, 64).toInt()};
                                        addNoteToBuffer(onBuffer, subnote, velocity, true, overrideChannel);
                                        addNoteToBuffer(offBuffer, subnote, velocity, false, overrideChannel);
                                    }
                                }
                            }
                        } else if (subnotes.count() > 0) {
                            for (const QVariant &subnoteVar : subnotes) {
                                const Note *subnote = subnoteVar.value<Note*>();
                                if (subnote) {
                                    addNoteToBuffer(onBuffer, subnote, 64, true, overrideChannel);
                                    addNoteToBuffer(offBuffer, subnote, 64, false, overrideChannel);
                                }
                            }
                        } else {
                            addNoteToBuffer(onBuffer, note, 64, true, overrideChannel);
                            addNoteToBuffer(offBuffer, note, 64, false, overrideChannel);
                        }
                    }
                    d->onBuffers[nextPosition + (d->bankOffset * d->width)] = onBuffer;
                    d->offBuffers[nextPosition + (d->bankOffset * d->width)] = offBuffer;
                }
                if (!d->syncTimer) {
                    d->syncTimer = qobject_cast<SyncTimer*>(playGridManager()->syncTimer());
                }
                // If sequencePosition is -1, that means we're on the prefilling step and need to
                // adjust the delay so we're scheduling the notes onto the right position, otherwise
                // we're just posting messages for the next step
                const int delayAdjustment = (sequencePosition == -1) ? 2 : 1;
                switch (d->noteDestination) {
                    case PatternModel::SampleLoopedDestination:
                        // If this track is supposed to loop its sample, we are not supposed to be making patterny sounds
                        break;
                    case PatternModel::SampleTriggerDestination:
                    case PatternModel::SampleSlicedDestination:
                    {
                        // Only actually schedule notes for the next tick, not for the far-ahead...
                        if (progressionIncrement == 1) {
                            const juce::MidiBuffer &onBuffer = d->onBuffers[nextPosition + (d->bankOffset * d->width)];
                            const juce::MidiBuffer &offBuffer = d->offBuffers[nextPosition + (d->bankOffset * d->width)];
                            if (!onBuffer.isEmpty()) {
                                for (const juce::MidiMessageMetadata &meta : onBuffer) {
                                    ClipAudioSource *clip = d->clipForMidiNote(meta.data[1]);
                                    if (clip && (0x7F < meta.data[0] && meta.data[0] < 0xA0)) {
                                        ClipCommand *clipCommand{new ClipCommand};
                                        clipCommand->clip = clip;
                                        if (d->noteDestination == SampleSlicedDestination) {
                                            clipCommand->midiNote = 60;
                                            clipCommand->changeSlice = true;
                                            clipCommand->slice = clip->sliceForMidiNote(meta.data[1]);
                                        } else {
                                            clipCommand->midiNote = meta.data[1];
                                        }
                                        clipCommand->startPlayback = true;
                                        clipCommand->changeVolume = true;
                                        clipCommand->volume = float(meta.data[2]) / float(128);
                                        d->syncTimer->scheduleClipCommand(clipCommand, progressionIncrement);
                                    }
                                }
                            }
                            if (!offBuffer.isEmpty()) {
                                for (const juce::MidiMessageMetadata &meta : offBuffer) {
                                    ClipAudioSource *clip = d->clipForMidiNote(meta.data[1]);
                                    if (clip && (0x7F < meta.data[0] && meta.data[0] < 0xA0)) {
                                        ClipCommand *clipCommand{new ClipCommand};
                                        clipCommand->clip = clip;
                                        if (d->noteDestination == SampleSlicedDestination) {
                                            clipCommand->midiNote = 60;
                                            clipCommand->changeSlice = true;
                                            clipCommand->slice = clip->sliceForMidiNote(meta.data[1]);
                                        } else {
                                            clipCommand->midiNote = meta.data[1];
                                        }
                                        clipCommand->stopPlayback = true;
                                        d->syncTimer->scheduleClipCommand(clipCommand, progressionIncrement + noteDuration - delayAdjustment);
                                    }
                                }
                            }
                        }
                        break;
                    }
                    case PatternModel::SynthDestination:
                    default:
                        d->syncTimer->scheduleMidiBuffer(d->onBuffers[nextPosition + (d->bankOffset * d->width)], progressionIncrement - 1);
                        d->syncTimer->scheduleMidiBuffer(d->offBuffers[nextPosition + (d->bankOffset * d->width)], progressionIncrement + noteDuration - delayAdjustment);
                        break;
                }
            }
        }
    }
}

void PatternModel::updateSequencePosition(quint64 sequencePosition)
{
    // Don't play notes on channel 15, because that's the control channel, and we don't want patterns to play to that
    if ((isPlaying() && ((d->midiChannel > -1 && d->midiChannel < 15) || d->playGridManager->currentMidiChannel() > -1)) || sequencePosition == 0) {
        bool relevantToUs{false};
        quint64 nextPosition = sequencePosition;
        // Potentially it'd be tempting to try and optimise this manually to use bitwise operators,
        // but GCC already does that for you at -O2, so don't bother :)
        switch (d->noteLength) {
        case 1:
            if (nextPosition % 32 == 0) {
                relevantToUs = true;
                nextPosition = nextPosition / 32;
            }
            break;
        case 2:
            if (nextPosition % 16 == 0) {
                relevantToUs = true;
                nextPosition = nextPosition / 16;
            }
            break;
        case 3:
            if (nextPosition % 8 == 0) {
                relevantToUs = true;
                nextPosition = nextPosition / 8;
            }
            break;
        case 4:
            if (nextPosition % 4 == 0) {
                relevantToUs = true;
                nextPosition = nextPosition / 4;
            }
            break;
        case 5:
            if (nextPosition % 2 == 0) {
                relevantToUs = true;
                nextPosition = nextPosition / 2;
            }
            break;
        case 6:
            relevantToUs = true;
            break;
        default:
            qWarning() << "Incorrect note length in pattern, no notes will be played from this one, ever" << objectName();
            break;
        }

        if (relevantToUs) {
            nextPosition = nextPosition % (d->availableBars * d->width);
            int row = (nextPosition / d->width) % d->availableBars;
            int column = nextPosition - (row * d->width);
            d->playingRow = row + d->bankOffset;
            d->playingColumn = column;
            QMetaObject::invokeMethod(this, "playingRowChanged", Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, "playingColumnChanged", Qt::QueuedConnection);
        }
    }
}

void PatternModel::handleSequenceStop()
{
    // Unschedule any notes we've previously scheduled, to try and alleviate troublesome situations
}

void PatternModel::handleNotesChanging()
{
    // If orphaned, or the sequence is asking for sounds to happen, make sounds
    // But also, don't make sounds unless we're sample-triggering or slicing (otherwise the synths will handle it)
    if ((!d->sequence || d->sequence->shouldMakeSounds()) && (d->noteDestination == SampleTriggerDestination || d->noteDestination == SampleSlicedDestination)) {
        const QVariantList &mrcn{d->playGridManager->mostRecentlyChangedNotes()};
        if (mrcn.count() > 0) {
            const QVariantMap &metadata{mrcn.constLast().toMap()};

            static const QLatin1String note_on{"note_on"};
            static const QLatin1String note_off{"note_off"};
            const int midiChannel = metadata.value("channel").toInt();
            int midiNote = metadata.value("note").toInt();
            ClipAudioSource *clip = d->clipForMidiNote(midiNote);
            if (midiChannel == d->midiChannel && clip) {
                const QString messageType = metadata.value("type").toString();
                // Not handling these yet...
                const float velocity = metadata.value("velocity").toFloat();
                int slice{-1};
                if (d->noteDestination == SampleSlicedDestination) {
                    slice = clip->sliceForMidiNote(midiNote);
                    midiNote = 60;
                }
                if (messageType == note_on) {
                    ClipCommand *clipCommand{new ClipCommand};
                    clipCommand->clip = clip;
                    clipCommand->midiNote = midiNote;
                    clipCommand->startPlayback = true;
                    clipCommand->changeVolume = true;
                    clipCommand->volume = velocity / float(128);
                    if (slice > -1) {
                        clipCommand->changeSlice = true;
                        clipCommand->slice = slice;
                    }
                    d->samplerSynth->handleClipCommand(clipCommand);
                } else if (messageType == note_off) {
                    ClipCommand *clipCommand{new ClipCommand};
                    clipCommand->clip = clip;
                    clipCommand->midiNote = midiNote;
                    if (slice > -1) {
                        clipCommand->changeSlice = true;
                        clipCommand->slice = slice;
                    }
                    clipCommand->stopPlayback = true;
                    d->samplerSynth->handleClipCommand(clipCommand);
                }
            }
        }
    }
}
