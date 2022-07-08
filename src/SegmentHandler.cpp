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
#include "SyncTimer.h"
#include "TimerCommand.h"

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
    SegmentHandlerPrivate() {
        syncTimer = qobject_cast<SyncTimer*>(SyncTimer_instance());
        playGridManager = PlayGridManager::instance();
    }
    SyncTimer* syncTimer{nullptr};
    PlayGridManager* playGridManager{nullptr};
    QObject* zlSong{nullptr};
    ZLSegmentHandlerSynchronisationManager *zlSyncManager{nullptr};
    bool songMode{false};

    PlayfieldState playfieldState;
    quint64 playhead{0};
    QHash<quint64, QList<TimerCommand*> > playlist;

    void progressPlayback() {
        if (syncTimer->timerRunning()) {
            // Instead of using cumulative beat, we keep this one in hand so we don't have to juggle offsets of we start somewhere uneven
            ++playhead;
            if (playlist.contains(playhead)) {
                const QList<TimerCommand*> commands = playlist[playhead];
                for (TimerCommand* command : commands) {
                    syncTimer->scheduleTimerCommand(0, command);
                }
            }
        }
    }

    inline void handleTimerCommand(TimerCommand* command) {
        // Yes, these are dangerous, but also we really, really want this to be fast
        if (command->operation == TimerCommand::StartPartOperation) {
            playfieldState.trackStates[command->parameter]->sketchStates[command->parameter2]->partStates[command->parameter3] = true;
        } else if(command->operation == TimerCommand::StopPartOperation) {
            playfieldState.trackStates[command->parameter]->sketchStates[command->parameter2]->partStates[command->parameter3] = false;
        }
    }

    void movePlayhead(quint64 newPosition) {
        // Cycle through all positions from the current playhead
        // position to the new one and handle them all - but only
        // if the new position's actually different to the old one
        if (newPosition != playhead) {
            int direction = (playhead > newPosition) ? -1 : 1;
            while (playhead != newPosition) {
                playhead = playhead + direction;
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
    { };
    SegmentHandler *q{nullptr};
    SegmentHandlerPrivate* d{nullptr};
    QObject *zlSong{nullptr};
    QObject *zlMixesModel{nullptr};

    void setZlSong(QObject *newZlSong) {
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
        if (zlMixesModel != newZLMixesModel) {
            if (zlMixesModel) {
                zlMixesModel->disconnect(this);
            }
            zlMixesModel = newZLMixesModel;
            if (zlMixesModel) {
                connect(zlMixesModel, SIGNAL(songModeChanged()), this, SLOT(songModeChanged()), Qt::QueuedConnection);
                connect(zlMixesModel, SIGNAL(selectedMixIndexChanged()), this, SLOT(supdateSegments()), Qt::QueuedConnection);
                songModeChanged();
            }
        }
    }
public Q_SLOTS:
    void songModeChanged() {
        d->songMode = zlMixesModel->property("songMode").toBool();
        Q_EMIT q->songModeChanged();
    }
    void updateSegments() {
        QHash<quint64, QList<TimerCommand*> > playlist;
        int mixIndex = zlMixesModel->property("selectedMixIndex").toInt();
        QObject *mix{nullptr};
        QMetaObject::invokeMethod(zlMixesModel, "getMix", Qt::DirectConnection, Q_RETURN_ARG(QObject*, mix), Q_ARG(int, mixIndex));
        if (mix) {
            QObject *segmentsModel = mix->property("segmentsModel").value<QObject*>();
            if (segmentsModel) {
                // The position of the next set of commands to be added to the hash
                quint64 segmentPosition{0};
                QList<QObject*> clipsInPrevious;
                int segmentCount = segmentsModel->property("count").toInt();
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
                                // If the clip was not there in the previous step, that means we should turn it on
                                TimerCommand* command = new TimerCommand;
                                command->operation = TimerCommand::StartPartOperation;
                                command->parameter = clip->property("row").toInt();
                                command->parameter2 = clip->property("column").toInt();
                                command->parameter3 = clip->property("part").toInt();
                                commands << command;
                            }
                        }
                        for (QObject *clip : clipsInPrevious) {
                            if (!includedClips.contains(clip)) {
                                // If the clip was in the previous step, but not in this step, that means it
                                // should be turned off when reaching this position
                                TimerCommand* command = new TimerCommand;
                                command->operation = TimerCommand::StopPartOperation;
                                command->parameter = clip->property("row").toInt();
                                command->parameter2 = clip->property("column").toInt();
                                command->parameter3 = clip->property("part").toInt();
                                commands << command;
                            }
                        }
                        clipsInPrevious = includedClips;
                        playlist[segmentPosition] = commands;
                        // Finally, make sure the next step is covered
                        quint64 segmentDuration = ((segment->property("barLength").toInt() * 4) + segment->property("beatLength").toInt()) * d->syncTimer->getMultiplier();
                        segmentPosition += segmentDuration;
                    }
                }
                // And finally, add one stop command right at the end, so playback will stop itself when we get to the end of the song
                TimerCommand *stopCommand = new TimerCommand;
                stopCommand->operation = TimerCommand::StopPlaybackOperation;
                playlist[segmentPosition] = QList<TimerCommand*>{stopCommand};
            }
        } else {
            qWarning() << Q_FUNC_INFO << "Failed to get the current mix";
        }
        d->playlist = playlist;
    }
};

SegmentHandler::SegmentHandler(QObject *parent)
    : QObject(parent)
    , d(new SegmentHandlerPrivate)
{
    d->zlSyncManager = new ZLSegmentHandlerSynchronisationManager(d, this);
    connect(d->playGridManager, &PlayGridManager::metronomeBeat128thChanged, this, [this](){ d->progressPlayback(); });
    connect(d->syncTimer, &SyncTimer::timerCommand, this, [this](TimerCommand* command){ d->handleTimerCommand(command); });
    connect(d->syncTimer, &SyncTimer::timerRunningChanged, this, [this](){
        if (!d->syncTimer->timerRunning()) {
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
    if (d->zlSong != song) {
        d->zlSong = song;
        Q_EMIT songChanged();
    }
}

QObject *SegmentHandler::song() const
{
    return d->zlSong;
}

bool SegmentHandler::songMode() const
{
    return d->songMode;
}

void SegmentHandler::startPlayback(quint64 startOffset)
{
    d->playfieldState = PlayfieldState();
    d->movePlayhead(startOffset);
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