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
#include "SegmentHandler.h"

#include <QDateTime>
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
#include <MidiRouter.h>
#include <SamplerSynth.h>
#include <SyncTimer.h>

class ZLPatternSynchronisationManager : public QObject {
Q_OBJECT
public:
    explicit ZLPatternSynchronisationManager(PatternModel *parent = 0)
        : QObject(parent)
        , q(parent)
    {
        layerDataPuller = new QTimer(this);
        layerDataPuller->setInterval(100);
        layerDataPuller->setSingleShot(true);
        connect(layerDataPuller, &QTimer::timeout, this, &ZLPatternSynchronisationManager::retrieveLayerData);
    };
    PatternModel *q{nullptr};
    QObject *zlTrack{nullptr};
    QObject *zlPart{nullptr};
    QObject *zlScene{nullptr};
    QTimer *layerDataPuller{nullptr};

    void setZlTrack(QObject *newZlTrack)
    {
        if (zlTrack != newZlTrack) {
            if (zlTrack) {
                zlTrack->disconnect(this);
            }
            zlTrack = newZlTrack;
            if (zlTrack) {
                connect(zlTrack, SIGNAL(track_audio_type_changed()), this, SLOT(trackAudioTypeChanged()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(externalMidiChannelChanged()), this, SLOT(externalMidiChannelChanged()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(samples_changed()), this, SLOT(updateSamples()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(selectedPartChanged()), this, SLOT(selectedPartChanged()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(chained_sounds_changed()), this, SLOT(chainedSoundsChanged()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(chained_sounds_changed()), layerDataPuller, SLOT(start()), Qt::QueuedConnection);
                connect(zlTrack, SIGNAL(recordingPopupActiveChanged()), this, SIGNAL(recordingPopupActiveChanged()), Qt::QueuedConnection);
                q->setMidiChannel(zlTrack->property("id").toInt());
                trackAudioTypeChanged();
                externalMidiChannelChanged();
                updateSamples();
                selectedPartChanged();
                layerDataPuller->start();
                chainedSoundsChanged();
            }
            Q_EMIT q->zlTrackChanged();
        }
    }

    void setZlPart(QObject *newZlPart)
    {
        if (zlPart != newZlPart) {
            if (zlPart) {
                zlPart->disconnect(this);
            }
            zlPart = newZlPart;
            Q_EMIT q->zlPartChanged();
            if (zlPart) {
                connect(zlPart, SIGNAL(samples_changed()), this, SLOT(updateSamples()), Qt::QueuedConnection);
                updateSamples();
            }
        }
    }

    void setZlScene(QObject *newZlScene)
    {
        if (zlScene != newZlScene) {
            if (zlScene) {
                zlScene->disconnect(this);
            }
            zlScene = newZlScene;
            if (zlScene) {
                connect(zlScene, SIGNAL(enabled_changed()), this, SLOT(sceneEnabledChanged()), Qt::QueuedConnection);
                // This seems superfluous...
//                 connect(zlTrack, SIGNAL(enabled_changed()), this, SLOT(selectedPartChanged()), Qt::QueuedConnection);
                sceneEnabledChanged();
            }
            Q_EMIT q->zlSceneChanged();
        }
    }

    Q_SIGNAL void recordingPopupActiveChanged();

public Q_SLOTS:
    void sceneEnabledChanged() {
        q->setEnabled(zlScene->property("enabled").toBool());
    }
    void trackAudioTypeChanged() {
        static const QLatin1String sampleTrig{"sample-trig"};
        static const QLatin1String sampleSlice{"sample-slice"};
        static const QLatin1String sampleLoop{"sample-loop"};
        static const QLatin1String external{"external"};
//         static const QLatin1String synth{"synth"}; // the default
        const QString trackAudioType = zlTrack->property("trackAudioType").toString();
        if (trackAudioType == sampleTrig) {
            q->setNoteDestination(PatternModel::SampleTriggerDestination);
        } else if (trackAudioType == sampleSlice) {
            q->setNoteDestination(PatternModel::SampleSlicedDestination);
        } else if (trackAudioType == sampleLoop) {
            q->setNoteDestination(PatternModel::SampleLoopedDestination);
        } else if (trackAudioType == external) {
            q->setNoteDestination(PatternModel::ExternalDestination);
        } else { // or in other words "if (trackAudioType == synth)"
            q->setNoteDestination(PatternModel::SynthDestination);
        }
    }
    void externalMidiChannelChanged() {
        q->setExternalMidiChannel(zlTrack->property("externalMidiChannel").toInt());
    }
    void selectedPartChanged() {
        SequenceModel *sequence = qobject_cast<SequenceModel*>(q->sequence());
        if (sequence) {
            const int trackId{zlTrack->property("id").toInt()};
            const int selectedPart{zlTrack->property("selectedPart").toInt()};
            sequence->setActiveTrack(trackId, selectedPart);
        }
    }
    void updateSamples() {
        QVariantList clipIds;
        if (zlTrack && zlPart) {
            const QVariantList trackSamples = zlTrack->property("samples").toList();
            const QVariantList partSamples = zlPart->property("samples").toList();
            for (const QVariant& partSample : partSamples) {
                const QObject *sample = trackSamples[partSample.toInt()].value<QObject*>();
                if (sample) {
                    const int sampleCppId = sample->property("cppObjId").toInt();
                    if (sampleCppId > -1) {
                        clipIds << sampleCppId;
                    }
                }
            }
        }
        q->setClipIds(clipIds);
    }
    void chainedSoundsChanged() {
        if (zlTrack) {
            QList<int> chainedSounds;
            const QVariantList trackChainedSounds = zlTrack->property("chainedSounds").toList();
            for (const QVariant &trackChainedSound : trackChainedSounds) {
                const int chainedSound = trackChainedSound.toInt();
                if (chainedSound > -1) {
                    chainedSounds << chainedSound;
                }
            }
            MidiRouter::instance()->setZynthianChannels(q->trackIndex(), chainedSounds);
        }
    }
    void retrieveLayerData() {
        if (zlTrack) {
            QString jsonSnapshot;
            QMetaObject::invokeMethod(zlTrack, "getTrackSoundSnapshotJson", Qt::DirectConnection, Q_RETURN_ARG(QString, jsonSnapshot));
            q->setLayerData(jsonSnapshot);
        }
    }
};

class PatternModel::Private {
public:
    Private() {
        playGridManager = PlayGridManager::instance();
        samplerSynth = SamplerSynth::instance();
    }
    ~Private() {}
    ZLPatternSynchronisationManager *zlSyncManager{nullptr};
    SegmentHandler *segmentHandler{nullptr};
    QHash<QString, qint64> lastSavedTimes;
    int width{16};
    PatternModel::NoteDestination noteDestination{PatternModel::SynthDestination};
    int midiChannel{15};
    int externalMidiChannel{-1};
    QString layerData;
    int defaultNoteDuration{0};
    int noteLength{3};
    int availableBars{1};
    int activeBar{0};
    int bankOffset{0};
    int bankLength{8};
    bool enabled{true};
    int playingRow{0};
    int playingColumn{0};
    int previouslyUpdatedMidiChannel{-1};

    // This bunch of lists is equivalent to the data found in each note, and is
    // stored per-position (index in the outer is row * width + column). The
    // must be cleared on any change of the notes (which should always be done
    // through setNote and setMetadata to ensure this). If they are not cleared
    // on changes, what ends up sent to SyncTimer during playback will not match
    // what the model contains. So, remember your pattern hygiene and clean your
    // buffers!
    // The inner hash contains commands for the given position, with the key being
    // the on-position delay (so that iterating over the hash gives the scheduling
    // delay for that buffer, and the buffer).
    QHash<int, QHash<int, juce::MidiBuffer> > positionBuffers;
    // Handy variable in case we want to adjust how far ahead we're looking sometime
    // in the future (right now it's one step ahead, but we could look further if we
    // wanted to)
    static const int lookaheadAmount{2};
    /**
     * \brief Invalidates the position buffers relevant to the given position
     * If you give -1 for the two position indicators, the entire list of buffers
     * will be invalidated.
     * This function is required to ensure that all buffers the position could
     * have an impact on (including those which are before it) are invalidated.
     * @param row The row of the position to invalidate
     * @param column The column of the position to invalidate
     */
   void invalidatePosition(int row = -1, int column = -1) {
        if (row == -1 || column == -1) {
            positionBuffers.clear();
        } else {
            const int basePosition = (row * width) + column;
            for (int subsequentNoteIndex = 0; subsequentNoteIndex < lookaheadAmount; ++subsequentNoteIndex) {
                // We clear backwards, just because might as well (by subtracting the subsequentNoteIndex from our base position)
                int ourPosition = (basePosition - subsequentNoteIndex) % (availableBars * width);
                positionBuffers.remove(ourPosition);
            }
        }
    }

    SyncTimer* syncTimer{nullptr};
    SequenceModel *sequence;
    int trackIndex{-1};
    int partIndex{-1};

    PlayGridManager *playGridManager{nullptr};
    SamplerSynth *samplerSynth{nullptr};

    int gridModelStartNote{48};
    int gridModelEndNote{64};
    NotesModel *gridModel{nullptr};
    NotesModel *clipSliceNotes{nullptr};
    QList< QPointer<ClipAudioSource> > clips;
    /**
     * This function will return all clip sin the list which has a
     * keyZoneStart higher or equal to the given midi note and a keyZoneEnd
     * lower or equal to the given midi note (that is, all clipa for
     * which the midi note is inside the keyzone).
     * @param midiNote The midi note to find a clip for
     * @return The list of clip audio source instances that matches the given midi note (list can be empty)
     */
    QList<ClipAudioSource*> clipsForMidiNote(int midiNote) const {
        QList<ClipAudioSource*> found;
        for (ClipAudioSource *needle : qAsConst(clips)) {
            if (needle && needle->keyZoneStart() <= midiNote && midiNote <= needle->keyZoneEnd()) {
                found << needle;
            }
        }
        return found;
    }
    /**
     * Returns a (potentially empty) list of ClipCommands which match the midi message passed to the function
     * @param meta A midi meta message representing a midi event
     * @return A list of ClipCommand instances matching the midi event (list can be empty)
     */
    QList<ClipCommand*> midiMessageToClipCommands(const juce::MidiMessageMetadata &meta) const {
        QList<ClipCommand*> commands;
        const QList<ClipAudioSource*> clips = clipsForMidiNote(meta.data[1]);
        for (ClipAudioSource *clip : clips) {
            ClipCommand *command = ClipCommand::trackCommand(clip, midiChannel);
            command->startPlayback = meta.data[0] > 0x8F;
            command->stopPlayback = meta.data[0] < 0x90;
            if (command->startPlayback) {
                command->changeVolume = true;
                command->volume = float(meta.data[2]) / float(128);
            }
            if (noteDestination == SampleSlicedDestination) {
                command->midiNote = 60;
                command->changeSlice = true;
                command->slice = clip->sliceForMidiNote(meta.data[1]);
            } else {
                command->midiNote = meta.data[1];
            }
            commands << command;
        }
        return commands;
    }
};

PatternModel::PatternModel(SequenceModel* parent)
    : NotesModel(parent ? parent->playGridManager() : nullptr)
    , d(new Private)
{
    d->zlSyncManager = new ZLPatternSynchronisationManager(this);
    d->segmentHandler = SegmentHandler::instance();
    connect(d->segmentHandler, &SegmentHandler::playfieldInformationChanged, this, [this](int track, int sketch, int part){
        if (d->sequence && track == d->trackIndex && part == d->partIndex && sketch == d->sequence->sceneIndex()) {
            Q_EMIT isPlayingChanged();
        }
    }, Qt::QueuedConnection);
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
                d->invalidatePosition();
            }
        });
        connect(d->sequence, &SequenceModel::isLoadingChanged, this, [=](){
            if (!d->sequence->isLoading()) {
                beginResetModel();
                endResetModel();
                gridModel();
                clipSliceNotes();
            }
        });
    }
    // This will force the creation of a whole bunch of rows with the desired width and whatnot...
    setHeight(16);

    connect(this, &PatternModel::noteDestinationChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::midiChannelChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::layerDataChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::noteLengthChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::availableBarsChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::activeBarChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::bankOffsetChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::bankLengthChanged, this, &NotesModel::registerChange);
    connect(this, &PatternModel::enabledChanged, this, &NotesModel::registerChange);

    connect(this, &QObject::objectNameChanged, this, &PatternModel::nameChanged);
    connect(this, &QObject::objectNameChanged, this, &PatternModel::thumbnailUrlChanged);
    connect(this, &NotesModel::lastModifiedChanged, this, &PatternModel::hasNotesChanged);
    connect(this, &NotesModel::lastModifiedChanged, this, &PatternModel::thumbnailUrlChanged);
    connect(this, &PatternModel::bankOffsetChanged, this, &PatternModel::thumbnailUrlChanged);
    connect(this, &PatternModel::bankLengthChanged, this, &PatternModel::thumbnailUrlChanged);
    static const int noteDestinationTypeId = qRegisterMetaType<NoteDestination>();
    Q_UNUSED(noteDestinationTypeId)

    // Called whenever the effective midi channel changes (so both the midi channel and the external midi channel)
    QTimer* midiChannelUpdater = new QTimer(this);
    midiChannelUpdater->setInterval(100);
    midiChannelUpdater->setSingleShot(true);
    connect(midiChannelUpdater, &QTimer::timeout, this, [this](){
        int actualChannel = d->noteDestination == PatternModel::ExternalDestination && d->externalMidiChannel > -1 ? d->externalMidiChannel : d->midiChannel;
        MidiRouter::RoutingDestination routerDestination{MidiRouter::ZynthianDestination};
        switch(d->noteDestination) {
            case PatternModel::SampleSlicedDestination:
            case PatternModel::SampleTriggerDestination:
                routerDestination = MidiRouter::SamplerDestination;
                break;
            case PatternModel::ExternalDestination:
                routerDestination = MidiRouter::ExternalDestination;
                break;
            case PatternModel::SampleLoopedDestination:
            case PatternModel::SynthDestination:
            default:
                // Default destination
                break;
        }
        if (zlTrack() && zlTrack()->property("recordingPopupActive").toBool()) {
            // Recording Popup is active. Do connect midi channel to allow recording even if track mode is trig/slice
            MidiRouter::instance()->setChannelDestination(d->midiChannel, MidiRouter::ZynthianDestination, actualChannel == d->midiChannel ? -1 : actualChannel);
        } else {
            MidiRouter::instance()->setChannelDestination(d->midiChannel, routerDestination, actualChannel == d->midiChannel ? -1 : actualChannel);
        }
        if (d->previouslyUpdatedMidiChannel != d->midiChannel) {
            startLongOperation();
            for (int row = 0; row < rowCount(); ++row) {
                for (int column = 0; column < columnCount(createIndex(row, 0)); ++column) {
                    Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
                    QVariantList newSubnotes;
                    if (oldCompound) {
                        const QVariantList &oldSubnotes = oldCompound->subnotes();
                        if (oldSubnotes.count() > 0) {
                            for (const QVariant &subnote :oldCompound->subnotes()) {
                                Note *oldNote = subnote.value<Note*>();
                                if (oldNote) {
                                    newSubnotes << QVariant::fromValue<QObject*>(playGridManager()->getNote(oldNote->midiNote(), d->midiChannel));
                                } else {
                                    // This really shouldn't happen - spit out a warning and slap in something unknown so we keep the order intact
                                    newSubnotes << QVariant::fromValue<QObject*>(playGridManager()->getNote(0, d->midiChannel));
                                    qWarning() << "Failed to convert a subnote value which must be a Note object to a Note object - something clearly isn't right.";
                                }
                            }
                            setNote(row, column, playGridManager()->getCompoundNote(newSubnotes));
                        }
                    }
                }
            }
            endLongOperation();
            d->invalidatePosition();
            d->previouslyUpdatedMidiChannel = d->midiChannel;
        }
    });
    connect(this, &PatternModel::midiChannelChanged, midiChannelUpdater, QOverload<>::of(&QTimer::start));
    connect(this, &PatternModel::externalMidiChannelChanged, midiChannelUpdater, QOverload<>::of(&QTimer::start));
    connect(this, &PatternModel::noteDestinationChanged, midiChannelUpdater, QOverload<>::of(&QTimer::start));
    connect(d->zlSyncManager, &ZLPatternSynchronisationManager::recordingPopupActiveChanged, midiChannelUpdater, QOverload<>::of(&QTimer::start));

    connect(d->playGridManager, &PlayGridManager::midiMessage, this, &PatternModel::handleMidiMessage, Qt::DirectConnection);
    connect(qobject_cast<SyncTimer*>(SyncTimer_instance()), &SyncTimer::clipCommandSent, this, [this](ClipCommand *clipCommand){
        for (ClipAudioSource *needle : qAsConst(d->clips)) {
            if (needle && needle == clipCommand->clip) {
                Note *note = qobject_cast<Note*>(PlayGridManager::instance()->getNote(clipCommand->midiNote, d->midiChannel));
                if (note) {
                    if (clipCommand->stopPlayback) {
                        note->setIsPlaying(false);
                    }
                    if (clipCommand->startPlayback) {
                        note->setIsPlaying(true);
                    }
                }
                break;
            }
        }
    }, Qt::QueuedConnection);
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

void PatternModel::insertSubnote(int row, int column, int subnoteIndex, QObject *note)
{
    if (row > -1 && row < height() && column > -1 && column < width() && note) {
        Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
        QVariantList subnotes;
        QVariantList metadata;
        int actualPosition{0};
        if (oldCompound) {
            subnotes = oldCompound->subnotes();
            metadata = getMetadata(row, column).toList();
            actualPosition = qMin(subnoteIndex, subnotes.count());
        }

        // Ensure the note is correct according to our midi channel settings
        Note *newNote = qobject_cast<Note*>(note);
        if (newNote->midiChannel() != d->midiChannel) {
            newNote = qobject_cast<Note*>(playGridManager()->getNote(newNote->midiNote(), d->midiChannel));
        }

        subnotes.insert(actualPosition, QVariant::fromValue<QObject*>(newNote));
        metadata.insert(actualPosition, QVariantHash());
        setNote(row, column, playGridManager()->getCompoundNote(subnotes));
        setMetadata(row, column, metadata);
    }
}

int PatternModel::insertSubnoteSorted(int row, int column, QObject* note)
{
    int newPosition{0};
    if (row > -1 && row < height() && column > -1 && column < width() && note) {
        Note *newNote = qobject_cast<Note*>(note);
        Note* oldCompound = qobject_cast<Note*>(getNote(row, column));
        QVariantList subnotes;
        QVariantList metadata;
        if (oldCompound) {
            subnotes = oldCompound->subnotes();
            metadata = getMetadata(row, column).toList();
            for (int i = 0; i < subnotes.count(); ++i) {
                const Note* subnote = subnotes[i].value<Note*>();
                if (subnote->midiNote() <= newNote->midiNote()) {
                    newPosition = i + 1;
                } else {
                    break;
                }
            }
        }

        // Ensure the note is correct according to our midi channel settings
        if (newNote->midiChannel() != d->midiChannel) {
            newNote = qobject_cast<Note*>(playGridManager()->getNote(newNote->midiNote(), d->midiChannel));
        }

        subnotes.insert(newPosition, QVariant::fromValue<QObject*>(newNote));
        metadata.insert(newPosition, QVariantHash());
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
            if (key.isEmpty()) {
                const QVariantHash rawMeta = metadata.at(subnote).toHash();
                QVariantMap qmlFriendlyMeta;
                for (const QString &key : rawMeta.keys()) {
                    qmlFriendlyMeta[key] = rawMeta[key];
                }
                result.setValue(qmlFriendlyMeta);
            } else {
                result.setValue(metadata.at(subnote).toHash().value(key));
            }
        }
    }
    return result;
}

void PatternModel::setNote(int row, int column, QObject* note)
{
    d->invalidatePosition(row, column);
    NotesModel::setNote(row, column, note);
}

void PatternModel::setMetadata(int row, int column, QVariant metadata)
{
    d->invalidatePosition(row, column);
    NotesModel::setMetadata(row, column, metadata);
}

void PatternModel::resetPattern(bool clearNotes)
{
    startLongOperation();
    setNoteDestination(PatternModel::SynthDestination);
    setExternalMidiChannel(-1);
    setDefaultNoteDuration(0);
    setNoteLength(3);
    setAvailableBars(1);
    setBankOffset(0);
    setBankLength(8);
    setGridModelStartNote(48);
    setGridModelEndNote(64);
    setWidth(16);
    if (clearNotes && hasNotes()) {
        setHeight(0);
    }
    setHeight(16);
    endLongOperation();
}

void PatternModel::clear()
{
    startLongOperation();
    const int oldHeight = height();
    setHeight(0);
    setHeight(oldHeight);
    endLongOperation();
}

void PatternModel::clearRow(int row)
{
    startLongOperation();
    for (int column = 0; column < d->width; ++column) {
        setNote(row, column, nullptr);
        setMetadata(row, column, QVariantList());
    }
    endLongOperation();
}

void PatternModel::clearBank(int bank)
{
    startLongOperation();
    for (int i = 0; i < bankLength(); ++i) {
        clearRow((bankLength() * bank) + i);
    }
    endLongOperation();
}

void PatternModel::setWidth(int width)
{
    startLongOperation();
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
    endLongOperation();
}

bool PatternModel::exportToFile(const QString &fileName) const
{
    bool success{false};
    QFile patternFile(fileName);
    if (!d->lastSavedTimes.contains(fileName) || d->lastSavedTimes[fileName] < lastModified()) {
        if (patternFile.open(QIODevice::WriteOnly)) {
            patternFile.write(playGridManager()->modelToJson(this).toUtf8());
            patternFile.close();
            success = true;
            d->lastSavedTimes[fileName] = QDateTime::currentMSecsSinceEpoch();
        }
    }
    return success;
}

QObject* PatternModel::sequence() const
{
    return d->sequence;
}

int PatternModel::trackIndex() const
{
    return d->trackIndex;
}

void PatternModel::setTrackIndex(int trackIndex)
{
    if (d->trackIndex != trackIndex) {
        d->trackIndex = trackIndex;
        Q_EMIT trackIndexChanged();
    }
}

int PatternModel::partIndex() const
{
    return d->partIndex;
}

QString PatternModel::partName() const
{
    static const QStringList partNames{"a", "b", "c", "d", "e"};
    return (d->partIndex > -1 && d->partIndex < partNames.length()) ? partNames[d->partIndex] : "";
}

void PatternModel::setPartIndex(int partIndex)
{
    if (d->partIndex != partIndex) {
        d->partIndex = partIndex;
        Q_EMIT partIndexChanged();
    }
}

QString PatternModel::thumbnailUrl() const
{
    return QString("image://pattern/%1/%2?%3").arg(objectName()).arg(QString::number(int(d->bankOffset / d->bankLength))).arg(lastModified());
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
        // Before switching the destination, first let's quickly send a little note off for aaaaall notes on this channel
        juce::MidiBuffer buffer;
        buffer.addEvent(juce::MidiMessage::allNotesOff(d->midiChannel + 1), 0);
        qobject_cast<SyncTimer*>(SyncTimer_instance())->sendMidiBufferImmediately(buffer);
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
    startLongOperation();
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
    d->invalidatePosition();
    endLongOperation();
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
        Q_EMIT midiChannelChanged();
    }
}

int PatternModel::midiChannel() const
{
    return d->midiChannel;
}

void PatternModel::setExternalMidiChannel(int externalMidiChannel)
{
    if (d->externalMidiChannel != externalMidiChannel) {
        d->externalMidiChannel = externalMidiChannel;
        Q_EMIT externalMidiChannelChanged();
    }
}

int PatternModel::externalMidiChannel() const
{
    return d->externalMidiChannel;
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

void PatternModel::setDefaultNoteDuration(int defaultNoteDuration)
{
    if (d->defaultNoteDuration != defaultNoteDuration) {
        d->defaultNoteDuration = defaultNoteDuration;
        Q_EMIT defaultNoteDurationChanged();
    }
}

int PatternModel::defaultNoteDuration() const
{
    return d->defaultNoteDuration;
}

void PatternModel::setNoteLength(int noteLength)
{
    if (d->noteLength != noteLength) {
        d->noteLength = noteLength;
        d->invalidatePosition();
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

bool PatternModel::bankHasNotes(int bankIndex) const
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

bool PatternModel::hasNotes() const
{
    bool hasNotes{false};
    for (int row = 0; row < rowCount(); ++row) {
        for (int column = 0; column < d->width; ++column) {
            Note* note = qobject_cast<Note*>(getNote(row , column));
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

bool PatternModel::currentBankHasNotes() const
{
    return bankHasNotes(floor(d->bankOffset / d->bankLength));
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
    if (clipIds.length() == d->clips.length()) {
        int i{0};
        for (const QVariant &clipId : clipIds) {
            if (!d->clips[i] || d->clips[i]->id() != clipId) {
                changed = true;
                break;
            }
            ++i;
        }
    } else {
        changed = true;
    }
    if (changed) {
        QList<QPointer<ClipAudioSource>> newClips;
        for (const QVariant &clipId: clipIds) {
            ClipAudioSource *newClip = ClipAudioSource_byID(clipId.toInt());
            if (newClip) {
                newClips << newClip;
                connect(newClip, &QObject::destroyed, this, [this, newClip](){ d->clips.removeAll(newClip); });
            }
        }
        d->clips = newClips;
        Q_EMIT clipIdsChanged();
    }
}

QVariantList PatternModel::clipIds() const
{
    QVariantList ids;
    for (ClipAudioSource *clip : qAsConst(d->clips)) {
        ids << clip->id();
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
        QTimer *refilTimer = new QTimer(d->gridModel);
        refilTimer->setInterval(100);
        refilTimer->setSingleShot(true);
        connect(refilTimer, &QTimer::timeout, d->gridModel, fillClipSliceNotes);
        connect(this, &PatternModel::clipIdsChanged, refilTimer, QOverload<>::of(&QTimer::start));
        connect(this, &PatternModel::midiChannelChanged, refilTimer, QOverload<>::of(&QTimer::start));
        refilTimer->start();
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
            // qDebug() << "Rebuilding" << d->gridModel << "for destination" << d->noteDestination << "for channel" << d->midiChannel;
            d->gridModel->startLongOperation();
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
            d->gridModel->endLongOperation();
        };
        QTimer *refilTimer = new QTimer(d->gridModel);
        refilTimer->setInterval(100);
        refilTimer->setSingleShot(true);
        connect(refilTimer, &QTimer::timeout, d->gridModel, rebuildGridModel);
        connect(this, &PatternModel::midiChannelChanged, refilTimer, QOverload<>::of(&QTimer::start));
        connect(this, &PatternModel::gridModelStartNoteChanged, refilTimer, QOverload<>::of(&QTimer::start));
        connect(this, &PatternModel::gridModelEndNoteChanged, refilTimer, QOverload<>::of(&QTimer::start));
        refilTimer->start();
    }
    return d->gridModel;
}

QObject *PatternModel::zlTrack() const
{
    return d->zlSyncManager->zlTrack;
}

void PatternModel::setZlTrack(QObject *zlTrack)
{
    d->zlSyncManager->setZlTrack(zlTrack);
}

QObject *PatternModel::zlPart() const
{
    return d->zlSyncManager->zlPart;
}

void PatternModel::setZlPart(QObject *zlPart)
{
    d->zlSyncManager->setZlPart(zlPart);
}

QObject *PatternModel::zlScene() const
{
    return d->zlSyncManager->zlScene;
}

void PatternModel::setZlScene(QObject *zlScene)
{
    d->zlSyncManager->setZlScene(zlScene);
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
    if (d->segmentHandler->songMode()) {
        isPlaying = d->segmentHandler->playfieldState(d->trackIndex, d->sequence->sceneIndex(), d->partIndex);
    } else if (d->sequence && d->sequence->isPlaying()) {
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

inline juce::MidiBuffer &getOrCreateBuffer(QHash<int, juce::MidiBuffer> &collection, int position)
{
    if (!collection.contains(position)) {
        collection[position] = juce::MidiBuffer();
    }
    return collection[position];
}

inline void noteLengthDetails(int noteLength, quint64 &nextPosition, bool &relevantToUs, quint64 &noteDuration)
{
    // Potentially it'd be tempting to try and optimise this manually to use bitwise operators,
    // but GCC already does that for you at -O2, so don't bother :)
    switch (noteLength) {
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
        qWarning() << "Incorrect note length in pattern, no notes will be played from this one, ever";
        break;
    }
}

void PatternModel::handleSequenceAdvancement(quint64 sequencePosition, int progressionLength, int initialProgression) const
{
    static const QLatin1String velocityString{"velocity"};
    static const QLatin1String delayString{"delay"};
    static const QLatin1String durationString{"duration"};
    if (isPlaying()
        // Play any note if the pattern is set to sliced or trigger destination, since then it's not sending things through the midi graph
        && (d->noteDestination == PatternModel::SampleSlicedDestination || d->noteDestination == PatternModel ::SampleTriggerDestination
        // Don't play notes on channel 15, because that's the control channel, and we don't want patterns to play to that
        || (d->midiChannel > -1 && d->midiChannel < 15)
        // And if we're playing midi, but don't have a good channel of our own, if the current channel is good, use that
        || d->playGridManager->currentMidiChannel() > -1)
    ) {
        const int overrideChannel{(d->midiChannel == 15) ? d->playGridManager->currentMidiChannel() : -1};
        const quint64 playbackOffset{d->segmentHandler->songMode() ? d->segmentHandler->playfieldOffset(d->trackIndex, d->sequence->sceneIndex(), d->partIndex) : 0};
        quint64 noteDuration{0};
        bool relevantToUs{false};
        // Since this happens at the /end/ of the cycle in a beat, this should be used to schedule beats for the next
        // beat, not the current one. That is to say, prepare the next frame, not the current one (since those notes
        // have already been played).
        for (int progressionIncrement = initialProgression; progressionIncrement <= progressionLength; ++progressionIncrement) {
            // check whether the sequencePosition + progressionIncrement matches our note length
            quint64 nextPosition = sequencePosition - playbackOffset + progressionIncrement;
            noteLengthDetails(d->noteLength, nextPosition, relevantToUs, noteDuration);

            if (relevantToUs) {
                // Get the next row/column combination, and schedule the previous one off, and the next one on
                // squish nextPosition down to fit inside our available range (d->availableBars * d->width)
                // start + (numberToBeWrapped - start) % (limit - start)
                nextPosition = nextPosition % (d->availableBars * d->width);

                if (!d->positionBuffers.contains(nextPosition + (d->bankOffset * d->width))) {
                    QHash<int, juce::MidiBuffer> positionBuffers;
                    // Do a lookup for any notes after this position that want playing before their step (currently
                    // just looking ahead one step, we could probably afford to do a bunch, but one for now)
                    for (int subsequentNoteIndex = 0; subsequentNoteIndex < d->lookaheadAmount; ++subsequentNoteIndex) {
                        int ourPosition = (nextPosition + subsequentNoteIndex) % (d->availableBars * d->width);
                        int row = (ourPosition / d->width) % d->availableBars;
                        int column = ourPosition - (row * d->width);
                        const Note *note = qobject_cast<const Note*>(getNote(row + d->bankOffset, column));
                        if (note) {
                            const QVariantList &subnotes = note->subnotes();
                            const QVariantList &meta = getMetadata(row + d->bankOffset, column).toList();
                            // The first note we want to treat to all the things
                            if (subsequentNoteIndex == 0) {
                                if (meta.count() == subnotes.count()) {
                                    for (int subnoteIndex = 0; subnoteIndex < subnotes.count(); ++subnoteIndex) {
                                        const Note *subnote = subnotes[subnoteIndex].value<Note*>();
                                        const QVariantHash &metaHash = meta[subnoteIndex].toHash();
                                        if (subnote) {
                                            if (metaHash.isEmpty()) {
                                                addNoteToBuffer(getOrCreateBuffer(positionBuffers, 0), subnote, 64, true, overrideChannel);
                                                addNoteToBuffer(getOrCreateBuffer(positionBuffers, noteDuration), subnote, 64, false, overrideChannel);
                                            } else {
                                                const int velocity{metaHash.value(velocityString, 64).toInt()};
                                                const int delay{metaHash.value(delayString, 0).toInt()};
                                                int duration{metaHash.value(durationString, noteDuration).toInt()};
                                                if (duration < 1) {
                                                    duration = noteDuration;
                                                }
                                                addNoteToBuffer(getOrCreateBuffer(positionBuffers, delay), subnote, velocity, true, overrideChannel);
                                                addNoteToBuffer(getOrCreateBuffer(positionBuffers, delay + duration), subnote, velocity, false, overrideChannel);
                                            }
                                        }
                                    }
                                } else if (subnotes.count() > 0) {
                                    for (const QVariant &subnoteVar : subnotes) {
                                        const Note *subnote = subnoteVar.value<Note*>();
                                        if (subnote) {
                                            addNoteToBuffer(getOrCreateBuffer(positionBuffers, 0), subnote, 64, true, overrideChannel);
                                            addNoteToBuffer(getOrCreateBuffer(positionBuffers, noteDuration), subnote, 64, false, overrideChannel);
                                        }
                                    }
                                } else {
                                    addNoteToBuffer(getOrCreateBuffer(positionBuffers, 0), note, 64, true, overrideChannel);
                                    addNoteToBuffer(getOrCreateBuffer(positionBuffers, noteDuration), note, 64, false, overrideChannel);
                                }
                            // The lookahead notes only need handling if, and only if, there is matching meta, and the delay is negative (as in, position before that step)
                            } else {
                                if (meta.count() == subnotes.count()) {
                                    const int positionAdjustment = subsequentNoteIndex * noteDuration;
                                    for (int subnoteIndex = 0; subnoteIndex < subnotes.count(); ++subnoteIndex) {
                                        const Note *subnote = subnotes[subnoteIndex].value<Note*>();
                                        const QVariantHash &metaHash = meta[subnoteIndex].toHash();
                                        if (subnote) {
                                            if (!metaHash.isEmpty() && metaHash.contains(delayString)) {
                                                const int delay{metaHash.value(delayString, 0).toInt()};
                                                if (delay < 0) {
                                                    const int velocity{metaHash.value(velocityString, 64).toInt()};
                                                    int duration{metaHash.value(durationString, noteDuration).toInt()};
                                                    if (duration < 1) {
                                                        duration = noteDuration;
                                                    }
//                                                     qDebug() << "Next position" << nextPosition << "with ourPosition" << ourPosition << "where delay" << delay << "is less than 0";
//                                                     qDebug() << "With position adjustment" << positionAdjustment << "we end up with start" << positionAdjustment + delay << "and end" << positionAdjustment + delay + duration;
                                                    addNoteToBuffer(getOrCreateBuffer(positionBuffers, positionAdjustment + delay), subnote, velocity, true, overrideChannel);
                                                    addNoteToBuffer(getOrCreateBuffer(positionBuffers, positionAdjustment + delay + duration), subnote, velocity, false, overrideChannel);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    d->positionBuffers[nextPosition + (d->bankOffset * d->width)] = positionBuffers;
                }
                if (!d->syncTimer) {
                    d->syncTimer = qobject_cast<SyncTimer*>(playGridManager()->syncTimer());
                }
                switch (d->noteDestination) {
                    case PatternModel::SampleLoopedDestination:
                        // If this track is supposed to loop its sample, we are not supposed to be making patterny sounds
                        break;
                    case PatternModel::SampleTriggerDestination:
                    case PatternModel::SampleSlicedDestination:
                        // Both sample-trig and sample-slice send midi information through, and those notes then get fired by
                        // SyncTimer as usual, and caught in handleMidiMessage() below
                    case PatternModel::ExternalDestination:
                        // While external destination /is/ somewhere else, libzl's MidiRouter does the actual work of the somewhere-else-ness
                        // We set this up in the midiChannelUpdater timeout handler (see PatternModel's ctor)
                    case PatternModel::SynthDestination:
                    default:
                    {
                        const QHash<int, juce::MidiBuffer> &positionBuffers = d->positionBuffers[nextPosition + (d->bankOffset * d->width)];
                        QHash<int, juce::MidiBuffer>::const_iterator position;
                        for (position = positionBuffers.constBegin(); position != positionBuffers.constEnd(); ++position) {
                            d->syncTimer->scheduleMidiBuffer(position.value(), qMax(0, progressionIncrement + position.key()));
                        }
                        break;
                    }
                }
            }
        }
    }
}

void PatternModel::updateSequencePosition(quint64 sequencePosition)
{
    // Don't play notes on channel 15, because that's the control channel, and we don't want patterns to play to that
    if ((isPlaying()
            && (d->noteDestination == PatternModel::SampleSlicedDestination || d->noteDestination == PatternModel ::SampleTriggerDestination
            || (d->midiChannel > -1 && d->midiChannel < 15)
            || d->playGridManager->currentMidiChannel() > -1))
        || sequencePosition == 0
    ) {
        bool relevantToUs{false};
        quint64 nextPosition{sequencePosition};
        quint64 noteDuration{0};
        noteLengthDetails(d->noteLength, nextPosition, relevantToUs, noteDuration);

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

void PatternModel::handleMidiMessage(const unsigned char &byte1, const unsigned char &byte2, const unsigned char &byte3)
{
    // If orphaned, or the sequence is asking for sounds to happen, make sounds
    if ((!d->sequence || (d->sequence->shouldMakeSounds() && (d->sequence->soloPatternObject() == this || d->enabled)))
        // But also, don't make sounds unless we're sample-triggering or slicing (otherwise the synths will handle it)
        && (d->noteDestination == SampleTriggerDestination || d->noteDestination == SampleSlicedDestination)) {
        if (0x7F < byte1 && byte1 < 0xA0) {
            juce::MidiMessage message(byte1, byte2, byte3);
            juce::MidiMessageMetadata meta(message.getRawData(), message.getRawDataSize(), 0);
            // Always remember, juce thinks channels are 1-indexed
            // FIXME We've got a problem - why is the "dunno" channel 9? There's a track there, that's going to cause issues...
            if (message.isForChannel(d->midiChannel + 1) || ((d->midiChannel < 0 || d->midiChannel > 8) && message.getChannel() == 10)) {
                const QList<ClipCommand*> commands = d->midiMessageToClipCommands(meta);
                for (ClipCommand *command : commands) {
                    d->samplerSynth->handleClipCommand(command);
                }
            }
        }
    }
}

// Since we got us a qobject up there a bit that we need to mocify
#include "PatternModel.moc"
