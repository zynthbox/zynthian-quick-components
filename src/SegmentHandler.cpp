/*
 * Copyright (C) 2022 Dan Leinir Turthra Jensen <admin@leinir.dk>
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

#include "SegmentHandler.h"
#include "PlayGridManager.h"
#include "SequenceModel.h"

#include "libzl.h"
// Hackety hack - we don't need all the thing, just need to convince CAS it exists
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1
#include "ClipAudioSource.h"
#include "ClipCommand.h"
#include "SyncTimer.h"
#include "TimerCommand.h"

#include <QDebug>
#include <QTimer>
#include <QVariant>

#define PartCount 5
#define TrackCount 10
#define ChannelCount 10
struct TrackState {
    TrackState() {
        for (int partIndex = 0; partIndex < PartCount; ++partIndex) {
            partStates[partIndex] = false;
            partOffset[partIndex] = 0;
        }
    }
    // Whether or not the specified part should be making sounds right now
    bool partStates[PartCount];
    quint64 partOffset[PartCount];
};
struct ChannelState {
    ChannelState() {
        for (int trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
            trackStates[trackIndex] = new TrackState();
        }
    }
    ~ChannelState() {
        for (int trackIndex = 0; trackIndex < TrackCount; ++trackIndex) {
            delete trackStates[trackIndex];
        }
    }
    TrackState* trackStates[TrackCount];
};
struct PlayfieldState {
    PlayfieldState() {
        for (int channelIndex = 0; channelIndex < ChannelCount; ++channelIndex) {
            channelStates[channelIndex] = new ChannelState();
        }
    };
    ~PlayfieldState() {
        for (int channelIndex = 0; channelIndex < ChannelCount; ++channelIndex) {
            delete channelStates[channelIndex];
        }
    }
    ChannelState* channelStates[ChannelCount];
};

class ZLSegmentHandlerSynchronisationManager;
class SegmentHandlerPrivate {
public:
    SegmentHandlerPrivate(SegmentHandler *q)
        : q(q)
    {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
        playGridManager = PlayGridManager::instance();
        playfieldState = new PlayfieldState();
    }
    SegmentHandler* q{nullptr};
    SyncTimer* syncTimer{nullptr};
    PlayGridManager* playGridManager{nullptr};
    ZLSegmentHandlerSynchronisationManager *zlSyncManager{nullptr};
    QList<SequenceModel*> sequenceModels;
    bool songMode{false};

    PlayfieldState *playfieldState{nullptr};
    quint64 playhead{0};
    QHash<quint64, QList<TimerCommand*> > playlist;
    QList<ClipAudioSource*> runningLoops;

    inline void ensureTimerClipCommand(TimerCommand* command) {
        if (command->dataParameter == nullptr) {
            // Since the clip command is swallowed each time, we'll need to reset it
            ClipCommand* clipCommand = syncTimer->getClipCommand();
            clipCommand->startPlayback = (command->operation == TimerCommand::StartClipLoopOperation); // otherwise, if statement above ensures it's a stop clip loop operation
            clipCommand->stopPlayback = !clipCommand->startPlayback;
            clipCommand->midiChannel = command->parameter;
            clipCommand->clip = ClipAudioSource_byID(command->parameter2);
            clipCommand->midiNote = command->parameter3;
            clipCommand->volume = clipCommand->clip->volumeAbsolute();
            clipCommand->looping = true;
            command->operation = TimerCommand::ClipCommandOperation;
            command->dataParameter = clipCommand;
            qDebug() << Q_FUNC_INFO << "Added clip command to timer command:" << command->dataParameter << clipCommand << "Start playback?" << clipCommand->startPlayback << "Stop playback?" << clipCommand->stopPlayback << clipCommand->midiChannel << clipCommand->midiNote << clipCommand->clip;
        }
    }

    void progressPlayback() {
        if (syncTimer->timerRunning() && songMode) {
            ++playhead;
            // Instead of using cumulative beat, we keep this one in hand so we don't have to juggle offsets of we start somewhere uneven
            if (playlist.contains(playhead)) {
                qDebug() << Q_FUNC_INFO << "Playhead is now at" << playhead << "and we have things to do";
                const QList<TimerCommand*> commands = playlist[playhead];
                for (TimerCommand* command : commands) {
                    if (command->operation == TimerCommand::StartClipLoopOperation || command->operation == TimerCommand::StopClipLoopOperation) {
                        if (command->parameter2 < 1) {
                            // If there's no clip to start or stop looping, we should really just ignore the command
                            continue;
                        }
                        ensureTimerClipCommand(command);
                    }
                    if (command->operation == TimerCommand::StartPartOperation || command->operation == TimerCommand::StopPartOperation) {
                        qDebug() << Q_FUNC_INFO << "Handling part start/stop operation immediately" << command;
                        handleTimerCommand(command);
                    } else if (command->operation == TimerCommand::StopPlaybackOperation) {
                        // Disconnect the global sequences, as we want them to stop making noises immediately
                        for (SequenceModel* sequence : qAsConst(sequenceModels)) {
                            sequence->disconnectSequencePlayback();
                        }
                        qDebug() << Q_FUNC_INFO << "Scheduled" << command;
                        syncTimer->scheduleTimerCommand(0, TimerCommand::cloneTimerCommand(command));
                    } else {
                        qDebug() << Q_FUNC_INFO << "Scheduled" << command;
                        syncTimer->scheduleTimerCommand(0, TimerCommand::cloneTimerCommand(command));
                    }
                }
            }
            Q_EMIT q->playheadChanged();
        }
    }

    inline void handleTimerCommand(TimerCommand* command) {
        // Yes, these are dangerous, but also we really, really want this to be fast
        if (command->operation == TimerCommand::StartPartOperation) {
//             qDebug() << Q_FUNC_INFO << "Timer command says to start part" << command->parameter << command->parameter2 << command->parameter3;
            playfieldState->channelStates[command->parameter]->trackStates[command->parameter2]->partStates[command->parameter3] = true;
            playfieldState->channelStates[command->parameter]->trackStates[command->parameter2]->partOffset[command->parameter3] = command->bigParameter;
            Q_EMIT q->playfieldInformationChanged(command->parameter, command->parameter2, command->parameter3);
        } else if(command->operation == TimerCommand::StopPartOperation) {
//             qDebug() << Q_FUNC_INFO << "Timer command says to stop part" << command->parameter << command->parameter2 << command->parameter3;
            playfieldState->channelStates[command->parameter]->trackStates[command->parameter2]->partStates[command->parameter3] = false;
            Q_EMIT q->playfieldInformationChanged(command->parameter, command->parameter2, command->parameter3);
        } else if (command->operation == TimerCommand::StopPlaybackOperation) {
            q->stopPlayback();
        }
    }

    void movePlayhead(quint64 newPosition, bool ignoreStop = false) {
        // Cycle through all positions from the current playhead
        // position to the new one and handle them all - but only
        // if the new position's actually different to the old one
        if (newPosition != playhead) {
            qDebug() << Q_FUNC_INFO << "Moving playhead from" << playhead << "to" << newPosition;
            int direction = (playhead > newPosition) ? -1 : 1;
            while (playhead != newPosition) {
                playhead = playhead + direction;
//                 qDebug() << Q_FUNC_INFO << "Moved playhead to" << playhead;
                if (playlist.contains(playhead)) {
                    const QList<TimerCommand*> commands = playlist[playhead];
                    for (TimerCommand* command : commands) {
                        if (ignoreStop && command->operation == TimerCommand::StopPlaybackOperation) {
                            continue;
                        } else if (command->operation == TimerCommand::StartClipLoopOperation || command->operation == TimerCommand::StopClipLoopOperation) {
                            // If there's no clip to start or stop looping, we should really just ignore the command
                            if (command->parameter2 > 0) {
                                TimerCommand *clonedCommand = TimerCommand::cloneTimerCommand(command);
                                ensureTimerClipCommand(clonedCommand);
                                syncTimer->scheduleTimerCommand(0, clonedCommand);
                            }
                        } else {
                            handleTimerCommand(command);
                        }
                    }
                }
            }
        }
        Q_EMIT q->playheadChanged();
    }
};

class ZLSegmentHandlerSynchronisationManager : public QObject {
Q_OBJECT
public:
    explicit ZLSegmentHandlerSynchronisationManager(SegmentHandlerPrivate *d, SegmentHandler *parent = 0)
        : QObject(parent)
        , q(parent)
        , d(d)
    {
        segmentUpdater.setInterval(100);
        segmentUpdater.setSingleShot(true);
        connect(&segmentUpdater, &QTimer::timeout, q, [this](){ updateSegments(); });
    };
    SegmentHandler *q{nullptr};
    SegmentHandlerPrivate* d{nullptr};
    QObject *zlSong{nullptr};
    QObject *zlSketchesModel{nullptr};
    QObject *zLSelectedSketch{nullptr};
    QObject *zLSegmentsModel{nullptr};
    QList<QObject*> zlChannels;
    QTimer segmentUpdater;

    void setZlSong(QObject *newZlSong) {
//         qDebug() << "Setting new song" << newZlSong;
        if (zlSong != newZlSong) {
            if (zlSong) {
                zlSong->disconnect(this);
                d->sequenceModels.clear();
            }
            zlSong = newZlSong;
            if (zlSong) {
                setZLSketchesModel(zlSong->property("sketchesModel").value<QObject*>());
                connect(zlSong, SIGNAL(isLoadingChanged()), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                fetchSequenceModels();
            }
            updateChannels();
        }
    }

    void setZLSketchesModel(QObject *newZLSketchesModel) {
//         qDebug() << Q_FUNC_INFO << "Setting new sketches model:" << newZLSketchesModel;
        if (zlSketchesModel != newZLSketchesModel) {
            if (zlSketchesModel) {
                zlSketchesModel->disconnect(this);
                zlSketchesModel->disconnect(&segmentUpdater);
            }
            zlSketchesModel = newZLSketchesModel;
            if (zlSketchesModel) {
                connect(zlSketchesModel, SIGNAL(songModeChanged()), this, SLOT(songModeChanged()), Qt::QueuedConnection);
                connect(zlSketchesModel, SIGNAL(selectedSketchIndexChanged()), this, SLOT(selectedSketchIndexChanged()), Qt::QueuedConnection);
                connect(zlSketchesModel, SIGNAL(clipAdded(int, int, QObject*)), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                connect(zlSketchesModel, SIGNAL(clipRemoved(int, int, QObject*)), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                songModeChanged();
                selectedSketchIndexChanged();
            }
        }
    }

    void setZLSelectedSketch(QObject *newSelectedSketch) {
        if (zLSelectedSketch != newSelectedSketch) {
            if (zLSelectedSketch) {
                zLSelectedSketch->disconnect(this);
                setZLSegmentsModel(nullptr);
            }
            zLSelectedSketch = newSelectedSketch;
            if (zLSelectedSketch) {
                setZLSegmentsModel(zLSelectedSketch->property("segmentsModel").value<QObject*>());
            }
        }
    }
    void setZLSegmentsModel(QObject *newSegmentsModel) {
        if (zLSegmentsModel != newSegmentsModel) {
            if (zLSegmentsModel) {
                zLSegmentsModel->disconnect(this);
            }
            zLSegmentsModel = newSegmentsModel;
            if (zLSegmentsModel) {
                connect(zLSegmentsModel, SIGNAL(countChanged()), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                connect(zLSegmentsModel, SIGNAL(totalBeatDurationChanged()), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                segmentUpdater.start();
            }
        }
    }
    void updateChannels() {
        if (zlChannels.count() > 0) {
            for (QObject* channel : zlChannels) {
                channel->disconnect(&segmentUpdater);
            }
            zlChannels.clear();
        }
        if (zlSong) {
            QObject *channelsModel = zlSong->property("channelsModel").value<QObject*>();
            for (int channelIndex = 0; channelIndex < 10; ++channelIndex) {
                QObject *channel{nullptr};
                QMetaObject::invokeMethod(channelsModel, "getChannel", Q_RETURN_ARG(QObject*, channel), Q_ARG(int, channelIndex));
                if (channel) {
                    zlChannels << channel;
                    connect(channel, SIGNAL(channel_audio_type_changed()), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                }
            }
//             qDebug() << Q_FUNC_INFO << "Updated channels, we now keep a hold of" << zlChannels.count();
            segmentUpdater.start();
        }
    }
public Q_SLOTS:
    void songModeChanged() {
        d->songMode = zlSketchesModel->property("songMode").toBool();
        // Since song mode playback is changed when we start and end playback, update the segments immediately, to ensure we're actually synced, otherwise we... won't be.
        updateSegments();
        Q_EMIT q->songModeChanged();
    }
    void selectedSketchIndexChanged() {
        int sketchIndex = zlSketchesModel->property("selectedSketchIndex").toInt();
        QObject *sketch{nullptr};
        QMetaObject::invokeMethod(zlSketchesModel, "getSketch", Qt::DirectConnection, Q_RETURN_ARG(QObject*, sketch), Q_ARG(int, sketchIndex));
        setZLSelectedSketch(sketch);
    }
    void fetchSequenceModels() {
        for (int i = 1; i < 11; ++i) {
            SequenceModel *sequence = qobject_cast<SequenceModel*>(d->playGridManager->getSequenceModel(QString("T%1").arg(i)));
            if (sequence) {
                d->sequenceModels << sequence;
            } else {
                qWarning() << Q_FUNC_INFO << "Sequence" << i << "could not be fetched, and will be unavailable for playback management";
            }
        }
    }
    void updateSegments() {
        static const QLatin1String sampleLoopedType{"sample-loop"};
        QHash<quint64, QList<TimerCommand*> > playlist;
        if (d->songMode && zLSegmentsModel && zlChannels.count() > 0) {
            // The position of the next set of commands to be added to the hash
            quint64 segmentPosition{0};
            QList<QObject*> clipsInPrevious;
            int segmentCount = zLSegmentsModel->property("count").toInt();
            qDebug() << Q_FUNC_INFO << "Working with" << segmentCount << "segments...";
            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                QObject *segment{nullptr};
                QMetaObject::invokeMethod(zLSegmentsModel, "get_segment", Q_RETURN_ARG(QObject*, segment), Q_ARG(int, segmentIndex));
                if (segment) {
                    QList<TimerCommand*> commands;
                    QVariantList clips = segment->property("clips").toList();
                    QList<QObject*> includedClips;
                    for (const QVariant &variantClip : clips) {
                        QObject *clip = variantClip.value<QObject*>();
                        includedClips << clip;
                        const bool shouldResetPlaybackposition{!clipsInPrevious.contains(clip)}; // This is currently always true for "not in previous segment", but likely we'll want to be able to explicitly do this as well (perhaps with an explicit offset even)
                        if (shouldResetPlaybackposition || !clipsInPrevious.contains(clip)) {
                            qDebug() << Q_FUNC_INFO << "The clip" << clip << "was not in the previous segment, so we should start playing it";
                            // If the clip was not there in the previous step, that means we should turn it on
                            TimerCommand* command = new TimerCommand; // This does not need to use the pool, as we might make a LOT of these, and also don't do so during playback time.
                            command->parameter = clip->property("row").toInt();
                            const QObject *channelObject = zlChannels.at(command->parameter);
                            const QString channelAudioType = channelObject->property("channelAudioType").toString();
                            if (channelAudioType == sampleLoopedType) {
                                command->operation = TimerCommand::StartClipLoopOperation;
                                command->parameter2 = clip->property("cppObjId").toInt();
                                command->parameter3 = 60;
                            } else {
                                command->operation = TimerCommand::StartPartOperation;
                                command->parameter2 = clip->property("column").toInt();
                                command->parameter3 = clip->property("part").toInt();
                                command->bigParameter = shouldResetPlaybackposition ? segmentPosition : 0;
                            }
                            commands << command;
                        } else {
                            qDebug() << Q_FUNC_INFO << "Clip was already in the previous segment, leaving in";
                        }
                    }
                    for (QObject *clip : clipsInPrevious) {
                        if (!includedClips.contains(clip)) {
                            qDebug() << Q_FUNC_INFO << "The clip" << clip << "was in the previous segment but not in this one, so we should stop playing that clip";
                            // If the clip was in the previous step, but not in this step, that means it
                            // should be turned off when reaching this position
                            TimerCommand* command = new TimerCommand; // This does not need to use the pool, as we might make a LOT of these, and also don't do so during playback time.
                            command->parameter = clip->property("row").toInt();
                            const QObject *channelObject = zlChannels.at(command->parameter);
                            const QString channelAudioType = channelObject->property("channelAudioType").toString();
                            if (channelAudioType == sampleLoopedType) {
                                command->operation = TimerCommand::StopClipLoopOperation;
                                command->parameter2 = clip->property("cppObjId").toInt();
                                command->parameter3 = 60;
                            } else {
                                command->operation = TimerCommand::StopPartOperation;
                                command->parameter2 = clip->property("column").toInt();
                                command->parameter3 = clip->property("part").toInt();
                            }
                            commands << command;
                        }
                    }
                    clipsInPrevious = includedClips;
                    // TODO Sort commands before adding - we really kind of want stop things before the start things, for when we have restarting added
                    playlist[segmentPosition] = commands;
                    // Finally, make sure the next step is covered
                    quint64 segmentDuration = ((segment->property("barLength").toInt() * 4) + segment->property("beatLength").toInt()) * d->syncTimer->getMultiplier();
                    segmentPosition += segmentDuration;
                } else {
                    qWarning() << Q_FUNC_INFO << "Failed to get segment" << segmentIndex;
                }
            }
            qDebug() << Q_FUNC_INFO << "Done processing segments, adding the final stops for any ongoing clips, and the timer stop command";
            // Run through the clipsInPrevious segment and add commands to stop them all
            QList<TimerCommand*> commands;
            for (QObject *clip : clipsInPrevious) {
                qDebug() << Q_FUNC_INFO << "The clip" << clip << "was in the final segment, so we should stop playing that clip at the end of playback";
                TimerCommand* command = new TimerCommand; // This does not need to use the pool, as we might make a LOT of these, and also don't do so during playback time.
                command->parameter = clip->property("row").toInt();
                const QObject *channelObject = zlChannels.at(command->parameter);
                const QString channelAudioType = channelObject->property("channelAudioType").toString();
                if (channelAudioType == sampleLoopedType) {
                    command->operation = TimerCommand::StopClipLoopOperation;
                    command->parameter2 = clip->property("cppObjId").toInt();
                    command->parameter3 = 60;
                } else {
                    command->operation = TimerCommand::StopPartOperation;
                    command->parameter2 = clip->property("column").toInt();
                    command->parameter3 = clip->property("part").toInt();
                }
                commands << command;
            }
            // And finally, add one stop command right at the end, so playback will stop itself when we get to the end of the song
            TimerCommand *stopCommand = d->syncTimer->getTimerCommand();
            stopCommand->operation = TimerCommand::StopPlaybackOperation;
            commands << stopCommand;
            playlist[segmentPosition] = commands;
        }
        d->playlist = playlist;
    }
};

SegmentHandler::SegmentHandler(QObject *parent)
    : QObject(parent)
    , d(new SegmentHandlerPrivate(this))
{
    d->zlSyncManager = new ZLSegmentHandlerSynchronisationManager(d, this);
    connect(d->syncTimer, &SyncTimer::timerCommand, this, [this](TimerCommand* command){ d->handleTimerCommand(command); }, Qt::DirectConnection);
    connect(d->syncTimer, &SyncTimer::clipCommandSent, this, [this](ClipCommand* command) {
        // We don't bother clearing stuff that's been stopped, stopping a non-running clip is essentially an nop anyway
        if (command->startPlayback && !d->runningLoops.contains(command->clip)) {
            d->runningLoops << command->clip;
        }
    }, Qt::DirectConnection);
    connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, [this](){
        if (!d->syncTimer->timerRunning()) {
            // First, stop any sounds currently running
            for (ClipAudioSource *clip : d->runningLoops) {
                ClipCommand *command = ClipCommand::noEffectCommand(clip);
                command->stopPlayback = true;
                d->syncTimer->scheduleClipCommand(command, 0);
                // Less than the best thing - having to do this to ensure we stop the ones looper
                // queued for starting as well, otherwise they'll get missed for stopping... We'll
                // want to handle this more precisely later, but for now this should do the trick.
                command = ClipCommand::effectedCommand(clip);
                command->stopPlayback = true;
                d->syncTimer->scheduleClipCommand(command, 0);
                for (int i = 0; i < 10; ++i) {
                    command = ClipCommand::channelCommand(clip, i);
                    command->midiNote = 60;
                    command->stopPlayback = true;
                    d->syncTimer->scheduleClipCommand(command, 0);
                }
            }
            // Then refresh the playfield
            delete d->playfieldState;
            d->playfieldState = new PlayfieldState();
        }
    }, Qt::QueuedConnection);
}

SegmentHandler::~SegmentHandler()
{
    delete d;
}

void SegmentHandler::setSong(QObject *song)
{
    if (d->zlSyncManager->zlSong != song) {
        d->zlSyncManager->setZlSong(song);
        Q_EMIT songChanged();
    }
}

QObject *SegmentHandler::song() const
{
    return d->zlSyncManager->zlSong;
}

bool SegmentHandler::songMode() const
{
    return d->songMode;
}

int SegmentHandler::playhead() const
{
    return d->playhead;
}

void SegmentHandler::startPlayback(quint64 startOffset, quint64 duration)
{
    if (d->playfieldState) {
        delete d->playfieldState;
    }
    d->playfieldState = new PlayfieldState();
    // If we're starting with a new playfield anyway, playhead's logically at 0, but also we need to handle the first position before we start playing (specifically so the sequences know what to do)
    d->playhead = 1;
    d->movePlayhead(0, true);
    d->movePlayhead(startOffset, true);
    if (duration > 0) {
        TimerCommand *stopCommand = d->syncTimer->getTimerCommand();
        stopCommand->operation = TimerCommand::StopPlaybackOperation;
        d->syncTimer->scheduleTimerCommand(duration, stopCommand);
    }
    // Hook up the global sequences to playback
    for (int i = 1; i < 11; ++i) {
        SequenceModel *sequence = qobject_cast<SequenceModel*>(d->playGridManager->getSequenceModel(QString("T%1").arg(i)));
        if (sequence) {
            sequence->prepareSequencePlayback();
        } else {
            qDebug() << Q_FUNC_INFO << "Sequence" << i << "could not be fetched, and playback could not be prepared";
        }
    }
    d->playGridManager->startMetronome();
}

void SegmentHandler::stopPlayback()
{
    // Disconnect the global sequences
    for (SequenceModel* sequence : qAsConst(d->sequenceModels)) {
        sequence->disconnectSequencePlayback();
    }
    d->playGridManager->stopMetronome();
    d->movePlayhead(0, true);
}

bool SegmentHandler::playfieldState(int channel, int track, int part) const
{
    return d->playfieldState->channelStates[channel]->trackStates[track]->partStates[part];
}

quint64 SegmentHandler::playfieldOffset(int channel, int track, int part) const
{
    return d->playfieldState->channelStates[channel]->trackStates[track]->partOffset[part];
}

void SegmentHandler::progressPlayback() const
{
    d->progressPlayback();
}

// Since we've got a QObject up at the top that wants mocing
#include "SegmentHandler.moc"
