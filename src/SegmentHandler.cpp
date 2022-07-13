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

struct SketchState {
    SketchState() {
        for (int partIndex = 0; partIndex < 5; ++partIndex) {
            partStates << false;
        }
    }
    // Whether or not the specified part should be making sounds right now
    QList<bool> partStates;
};
struct TrackState {
    TrackState() {
        for (int sketchIndex = 0; sketchIndex < 10; ++sketchIndex) {
            sketchStates << new SketchState;
        }
    }
    ~TrackState() {
        qDeleteAll(sketchStates);
    }
    QList<SketchState*> sketchStates;
};
struct PlayfieldState {
    PlayfieldState() {
        for (int trackIndex = 0; trackIndex < 10; ++trackIndex) {
            trackStates << new TrackState;
        }
    };
    ~PlayfieldState() {
        qDeleteAll(trackStates);
    }
    QList<TrackState*> trackStates;
};

class ZLSegmentHandlerSynchronisationManager;
class SegmentHandlerPrivate {
public:
    SegmentHandlerPrivate(SegmentHandler *q)
        : q(q)
    {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
        playGridManager = PlayGridManager::instance();
    }
    SegmentHandler* q{nullptr};
    SyncTimer* syncTimer{nullptr};
    PlayGridManager* playGridManager{nullptr};
    ZLSegmentHandlerSynchronisationManager *zlSyncManager{nullptr};
    bool songMode{false};

    PlayfieldState playfieldState;
    quint64 playhead{0};
    QHash<quint64, QList<TimerCommand*> > playlist;
    QList<ClipAudioSource*> runningLoops;

    void progressPlayback() {
        if (syncTimer->timerRunning() && songMode) {
            // Instead of using cumulative beat, we keep this one in hand so we don't have to juggle offsets of we start somewhere uneven
            qDebug() << Q_FUNC_INFO << "Playhead is now at" << playhead;
            if (playlist.contains(playhead)) {
                const QList<TimerCommand*> commands = playlist[playhead];
                for (TimerCommand* command : commands) {
                    if (command->operation == TimerCommand::StartClipLoopOperation || command->operation == TimerCommand::StopClipLoopOperation) {
                        // Since the clip command is swallowed each time, we'll need to reset it
                        ClipCommand* clipCommand = new ClipCommand();
                        clipCommand->startPlayback = (command->operation == TimerCommand::StartClipLoopOperation); // otherwise, if statement above ensures it's a stop clip loop operation
                        clipCommand->stopPlayback = !clipCommand->startPlayback;
                        clipCommand->midiChannel = command->parameter;
                        clipCommand->clip = ClipAudioSource_byID(command->parameter2);
                        clipCommand->midiNote = command->parameter3;
                        clipCommand->volume = clipCommand->clip->volumeAbsolute();
                        clipCommand->looping = true;
                        command->variantParameter.setValue<void*>(clipCommand);
                        qDebug() << Q_FUNC_INFO << "Added clip command to timer command:" << command->variantParameter << command->variantParameter.value<void*>() << clipCommand << "Start playback?" << clipCommand->startPlayback << "Stop playback?" << clipCommand->stopPlayback << clipCommand->midiChannel << clipCommand->midiNote << clipCommand->clip;
                    }
                    qDebug() << Q_FUNC_INFO << "Scheduled" << command;
                    syncTimer->scheduleTimerCommand(0, command);
                }
            }
            ++playhead;
        }
    }

    inline void handleTimerCommand(TimerCommand* command) {
        // Yes, these are dangerous, but also we really, really want this to be fast
        if (command->operation == TimerCommand::StartPartOperation) {
            qDebug() << Q_FUNC_INFO << "Timer command says to start part" << command->parameter << command->parameter2 << command->parameter3;
            playfieldState.trackStates[command->parameter]->sketchStates[command->parameter2]->partStates[command->parameter3] = true;
        } else if(command->operation == TimerCommand::StopPartOperation) {
            qDebug() << Q_FUNC_INFO << "Timer command says to stop part" << command->parameter << command->parameter2 << command->parameter3;
            playfieldState.trackStates[command->parameter]->sketchStates[command->parameter2]->partStates[command->parameter3] = false;
        } else if (command->operation == TimerCommand::StopPlaybackOperation) {
            q->stopPlayback();
        } else if (command->operation == TimerCommand::StartClipLoopOperation) {
            ClipCommand *clipCommand = static_cast<ClipCommand *>(command->variantParameter.value<void*>());
            runningLoops << clipCommand->clip;
        } else if (command->operation == TimerCommand::StopClipLoopOperation) {
            ClipCommand *clipCommand = static_cast<ClipCommand *>(command->variantParameter.value<void*>());
            runningLoops.removeOne(clipCommand->clip);
        }
    }

    void movePlayhead(quint64 newPosition) {
        // Cycle through all positions from the current playhead
        // position to the new one and handle them all - but only
        // if the new position's actually different to the old one
        if (newPosition != playhead) {
            qDebug() << Q_FUNC_INFO << "Moving playhead from" << playhead << "to" << newPosition;
            int direction = (playhead > newPosition) ? -1 : 1;
            while (playhead != newPosition) {
                playhead = playhead + direction;
                qDebug() << Q_FUNC_INFO << "Moved playhead to" << playhead;
                if (playlist.contains(playhead)) {
                    const QList<TimerCommand*> commands = playlist[playhead];
                    for (TimerCommand* command : commands) {
                        handleTimerCommand(command);
                    }
                }
            }
        }
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
        segmentUpdater.setInterval(1);
        segmentUpdater.setSingleShot(true);
        connect(&segmentUpdater, &QTimer::timeout, q, [this](){ updateSegments(); });
    };
    SegmentHandler *q{nullptr};
    SegmentHandlerPrivate* d{nullptr};
    QObject *zlSong{nullptr};
    QObject *zlMixesModel{nullptr};
    QTimer segmentUpdater;

    void setZlSong(QObject *newZlSong) {
        qDebug() << "Setting new song" << newZlSong;
        if (zlSong != newZlSong) {
            if (zlSong) {
                zlSong->disconnect(this);
            }
            zlSong = newZlSong;
            if (zlSong) {
                setZLMixesModel(zlSong->property("mixesModel").value<QObject*>());
            }
        }
    }

    void setZLMixesModel(QObject *newZLMixesModel) {
        qDebug() << Q_FUNC_INFO << "Setting new mixes model:" << newZLMixesModel;
        if (zlMixesModel != newZLMixesModel) {
            if (zlMixesModel) {
                zlMixesModel->disconnect(this);
            }
            zlMixesModel = newZLMixesModel;
            if (zlMixesModel) {
                connect(zlMixesModel, SIGNAL(songModeChanged()), this, SLOT(songModeChanged()), Qt::QueuedConnection);
                connect(zlMixesModel, SIGNAL(selectedMixIndexChanged()), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                connect(zlMixesModel, SIGNAL(clipAdded(int, int, QObject*)), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                connect(zlMixesModel, SIGNAL(clipRemoved(int, int, QObject*)), &segmentUpdater, SLOT(start()), Qt::QueuedConnection);
                songModeChanged();
                updateSegments();
            }
        }
    }
public Q_SLOTS:
    void songModeChanged() {
        d->songMode = zlMixesModel->property("songMode").toBool();
        Q_EMIT q->songModeChanged();
    }
    void updateSegments() {
        static const QLatin1String sampleLoopedType{"sample-loop"};
        QHash<quint64, QList<TimerCommand*> > playlist;
        int mixIndex = zlMixesModel->property("selectedMixIndex").toInt();
        QObject *mix{nullptr};
        QMetaObject::invokeMethod(zlMixesModel, "getMix", Qt::DirectConnection, Q_RETURN_ARG(QObject*, mix), Q_ARG(int, mixIndex));
        QObject *tracksModel = zlSong->property("tracksModel").value<QObject*>();
        if (mix) {
            QObject *segmentsModel = mix->property("segmentsModel").value<QObject*>();
            if (segmentsModel) {
                // The position of the next set of commands to be added to the hash
                quint64 segmentPosition{0};
                QList<QObject*> clipsInPrevious;
                int segmentCount = segmentsModel->property("count").toInt();
                qDebug() << Q_FUNC_INFO << "Working with" << segmentCount << "segments...";
                for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                    QObject *segment{nullptr};
                    QMetaObject::invokeMethod(segmentsModel, "get_segment", Q_RETURN_ARG(QObject*, segment), Q_ARG(int, segmentIndex));
                    if (segment) {
                        QList<TimerCommand*> commands;
                        QVariantList clips = segment->property("clips").toList();
                        QList<QObject*> includedClips;
                        for (const QVariant &variantClip : clips) {
                            QObject *clip = variantClip.value<QObject*>();
                            includedClips << clip;
                            if (!clipsInPrevious.contains(clip)) {
                                qDebug() << Q_FUNC_INFO << "The clip" << clip << "was not in the previous segment, so we should start playing it";
                                // If the clip was not there in the previous step, that means we should turn it on
                                TimerCommand* command = new TimerCommand;
                                command->parameter = clip->property("row").toInt();
                                QObject *trackObject{nullptr};
                                QMetaObject::invokeMethod(tracksModel, "getTrack", Q_RETURN_ARG(QObject*, trackObject), Q_ARG(int, command->parameter));
                                const QString trackAudioType = trackObject->property("trackAudioType").toString();
                                if (trackAudioType == sampleLoopedType) {
                                    command->operation = TimerCommand::StartClipLoopOperation;
                                    command->parameter2 = clip->property("cppObjId").toInt();
                                    command->parameter3 = 60;
                                } else {
                                    command->operation = TimerCommand::StartPartOperation;
                                    command->parameter2 = clip->property("column").toInt();
                                    command->parameter3 = clip->property("part").toInt();
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
                                TimerCommand* command = new TimerCommand;
                                command->parameter = clip->property("row").toInt();
                                QObject *trackObject{nullptr};
                                QMetaObject::invokeMethod(tracksModel, "getTrack", Q_RETURN_ARG(QObject*, trackObject), Q_ARG(int, command->parameter));
                                const QString trackAudioType = trackObject->property("trackAudioType").toString();
                                if (trackAudioType == sampleLoopedType) {
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
                    TimerCommand* command = new TimerCommand;
                    command->parameter = clip->property("row").toInt();
                    QObject *trackObject{nullptr};
                    QMetaObject::invokeMethod(tracksModel, "getTrack", Q_RETURN_ARG(QObject*, trackObject), Q_ARG(int, command->parameter));
                    const QString trackAudioType = trackObject->property("trackAudioType").toString();
                    if (trackAudioType == sampleLoopedType) {
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
                TimerCommand *stopCommand = new TimerCommand;
                stopCommand->operation = TimerCommand::StopPlaybackOperation;
                commands << stopCommand;
                playlist[segmentPosition] = commands;
            } else {
                qWarning() << Q_FUNC_INFO << "Failed to get the segment model";
            }
        } else {
            qWarning() << Q_FUNC_INFO << "Failed to get the current mix";
        }
        d->playlist = playlist;
    }
};

SegmentHandler::SegmentHandler(QObject *parent)
    : QObject(parent)
    , d(new SegmentHandlerPrivate(this))
{
    d->zlSyncManager = new ZLSegmentHandlerSynchronisationManager(d, this);
    connect(d->playGridManager, &PlayGridManager::metronomeBeat128thChanged, this, [this](){ d->progressPlayback(); }, Qt::DirectConnection);
    connect(d->syncTimer, &SyncTimer::timerCommand, this, [this](TimerCommand* command){ d->handleTimerCommand(command); }, Qt::DirectConnection);
    connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, [this](){
        if (!d->syncTimer->timerRunning()) {
            // First, stop any sounds currently running
            for (ClipAudioSource *clip : d->runningLoops) {
                clip->stop();
            }
            // Then refresh the playfield
            d->playfieldState = PlayfieldState();
        }
    });
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

void SegmentHandler::startPlayback(quint64 startOffset, quint64 duration)
{
    d->playfieldState = PlayfieldState();
    d->movePlayhead(startOffset);
    if (duration > 0) {
        TimerCommand *stopCommand = new TimerCommand;
        stopCommand->operation = TimerCommand::StopPlaybackOperation;
        d->syncTimer->scheduleTimerCommand(duration, stopCommand);
    }
    d->playGridManager->startMetronome();
}

void SegmentHandler::stopPlayback()
{
    d->playGridManager->stopMetronome();
}

bool SegmentHandler::playfieldState(int track, int sketch, int part) const
{
    return d->playfieldState.trackStates[track]->sketchStates[sketch]->partStates[part];
}

// Since we've got a QObject up at the top that wants mocing
#include "SegmentHandler.moc"
