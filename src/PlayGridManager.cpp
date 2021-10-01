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

#include "PlayGridManager.h"
#include "Note.h"
#include "NotesModel.h"
#include "SettingsContainer.h"

// ZynthiLoops library
#include <libzl/SyncTimer.h>

#include <QQmlEngine>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QList>
#include <QStandardPaths>

Q_GLOBAL_STATIC(QList<PlayGridManager*>, timer_callback_tickers)
void timer_callback(int beat) {
    for (PlayGridManager* pgm : *timer_callback_tickers) {
        pgm->metronomeTick(beat);
    }
}

class PlayGridManager::Private
{
public:
    Private(PlayGridManager *q) : q(q) {
        updatePlaygrids();
        connect(&watcher, &QFileSystemWatcher::directoryChanged, q, [this](){
            updatePlaygrids();
        });
    }
    PlayGridManager *q;
    QStringList playgrids;
    QVariantMap currentPlaygrids;
    int pitch{0};
    int modulation{0};
    QMap<QString, NotesModel*> notesModels;
    QList<Note*> notes;
    QMap<QString, SettingsContainer*> settingsContainers;
    QMap<Note*, int> noteStateMap;
    QVariantList mostRecentlyChangedNotes;

    SyncTimer *syncTimer{nullptr};
    int metronomeBeat4th{0};
    int metronomeBeat8th{0};
    int metronomeBeat16th{0};
    int metronomeBeat32nd{0};
    int metronomeBeat64th{0};
    int metronomeBeat128th{0};

    QFileSystemWatcher watcher;

    void updatePlaygrids()
    {
        static const QStringList searchlist{QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.local/share/zynthian/playgrids", "/home/pi/zynthian-ui/qml-ui/playgrids"};
        QStringList newPlaygrids;

        for (const QString &searchdir : searchlist) {
            QDir dir(searchdir);
            if (dir.exists()) {
                QDirIterator it(searchdir);
                while (it.hasNext()) {
                    const QFileInfo fi(it.next() + "/main.qml");
                    if (it.fileName() != "." && it.fileName() != "..") {
                        if (fi.exists()) {
                            newPlaygrids << fi.absolutePath();
                        } else {
                            qDebug() << Q_FUNC_INFO << "A stray directory that does not contain a main.qml file was found in one of the playgrid search locations: " << fi.absolutePath();
                        }
                    }
                }
            } else {
                // A little naughty, but knewstuff kind of removes directories once everything in it's gone
                dir.mkpath(searchdir);
            }
            if (!watcher.directories().contains(searchdir)) {
                watcher.addPath(searchdir);
            }
        }

        newPlaygrids.sort();
        // Start out by clearing known playgrids - it's a bit of a hack, but it ensures that for e.g. when updating a playgrid from the store, that will also be picked up and reloaded
        playgrids.clear();
        Q_EMIT q->playgridsChanged();
        playgrids = newPlaygrids;
        Q_EMIT q->playgridsChanged();
        qDebug() << Q_FUNC_INFO << "We now have the following known grids:" << playgrids;
    }

    Note *findExistingNote(int midiNote, int midiChannel) {
        Note *note{nullptr};
        for (Note *aNote : notes) {
            if (aNote->midiNote() == midiNote && aNote->midiChannel() == midiChannel) {
                note = aNote;
                break;
            }
        }
        return note;
    }
};

PlayGridManager::PlayGridManager(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
}

PlayGridManager::~PlayGridManager()
{
    delete d;
}

QStringList PlayGridManager::playgrids() const
{
    return d->playgrids;
}

void PlayGridManager::updatePlaygrids()
{
    d->updatePlaygrids();
}

QVariantMap PlayGridManager::currentPlaygrids() const
{
    return d->currentPlaygrids;
}

void PlayGridManager::setCurrentPlaygrid(const QString& section, int index)
{
    if (!d->currentPlaygrids.contains(section) || d->currentPlaygrids[section] != index) {
        d->currentPlaygrids[section] = index;
        Q_EMIT currentPlaygridsChanged();
    }
}

int PlayGridManager::pitch() const
{
    return d->pitch;
}

void PlayGridManager::setPitch(int pitch)
{
    if (d->pitch != pitch) {
        // TODO send midi command
        d->pitch = pitch;
        Q_EMIT pitchChanged();
    }
}

int PlayGridManager::modulation() const
{
    return d->modulation;
}

void PlayGridManager::setModulation(int modulation)
{
    if (d->modulation != modulation) {
        // TODO send midi command
        d->modulation = modulation;
        Q_EMIT modulationChanged();
    }
}

QObject* PlayGridManager::getNotesModel(const QString& name)
{
    NotesModel *model = d->notesModels.value(name);
    if (!model) {
        model = new NotesModel(this);
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        d->notesModels[name] = model;
    }
    return model;
}

QObject* PlayGridManager::getNote(int midiNote, int midiChannel)
{
    Note *note{nullptr};
    if (0 <= midiNote && midiNote <= 127 && 0 <= midiChannel && midiChannel <= 15) {
        for (Note *aNote : d->notes) {
            if (aNote->midiNote() == midiNote && aNote->midiChannel() == midiChannel) {
                note = aNote;
                break;
            }
        }
        if (!note) {
            static const QStringList note_int_to_str_map{"C", "C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            note = new Note(this);
            note->setName(note_int_to_str_map.value(midiNote % 12));
            note->setMidiNote(midiNote);
            note->setMidiChannel(midiChannel);
            QQmlEngine::setObjectOwnership(note, QQmlEngine::CppOwnership);
            d->notes << note;
        }
    }
    return note;
}

QObject* PlayGridManager::getCompoundNote(const QVariantList& notes)
{
    QObjectList actualNotes;
    for (const QVariant &var : notes) {
        actualNotes << var.value<QObject*>();
    }
    Note *note{nullptr};
    // Make the compound note's fake note value...
    int fake_midi_note = 128;
    for (QObject *subnote : actualNotes) {
        Note *actualSubnote = qobject_cast<Note*>(subnote);
        if (actualSubnote) {
            fake_midi_note = fake_midi_note + (127 * actualSubnote->midiNote());
        } else {
            // BAD CODER! THIS IS NOT A NOTE!
            fake_midi_note = -1;
            break;
        }
    }
    if (fake_midi_note > 127) {
        for (Note *aNote : d->notes) {
            if (aNote->midiNote() == fake_midi_note) {
                note = aNote;
                break;
            }
        }
        if (!note) {
            note = new Note(this);
            note->setMidiNote(fake_midi_note);
            note->setSubnotes(notes);
            QQmlEngine::setObjectOwnership(note, QQmlEngine::CppOwnership);
            d->notes << note;
        }
    }
    return note;
}

QObject* PlayGridManager::getSettingsStore(const QString& name)
{
    SettingsContainer *settings = d->settingsContainers.value(name);
    if (!settings) {
        settings = new SettingsContainer(name, this);
        QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
        d->settingsContainers[name] = settings;
    }
    return settings;
}

void PlayGridManager::setNotesOn(QVariantList notes, QVariantList velocities)
{
    if (notes.count() == velocities.count()) {
        for (int i = 0; i < notes.count(); ++i) {
            setNoteState(qobject_cast<Note*>(notes[i].value<QObject*>()), velocities[i].toInt(), true);
        }
    }
}

void PlayGridManager::setNotesOff(QVariantList notes)
{
    for (int i = 0; i < notes.count(); ++i) {
        setNoteState(qobject_cast<Note*>(notes[i].value<QObject*>()), 0, false);
    }
}
void PlayGridManager::setNoteOn(QObject* note, int velocity)
{
    setNoteState(qobject_cast<Note*>(note), velocity, true);
}

void PlayGridManager::setNoteOff(QObject* note)
{
    setNoteState(qobject_cast<Note*>(note), 0, false);
}

void PlayGridManager::setNoteState(Note* note, int velocity, bool setOn)
{
    if (note) {
        QObjectList subnotes;
        const QVariantList tempSubnotes = note->subnotes();
        for (const QVariant &tempSubnote : tempSubnotes) {
            subnotes << tempSubnote.value<QObject*>();
        }
        int subnoteCount = subnotes.count();
        if (subnoteCount > 0) {
            for (QObject *note : subnotes) {
                setNoteState(qobject_cast<Note*>(note), velocity, setOn);
            }
        } else {
            if (d->noteStateMap.contains(note)) {
                if (setOn) {
                    d->noteStateMap[note] = d->noteStateMap[note] + 1;
                } else {
                    d->noteStateMap[note] = d->noteStateMap[note] - 1;
                    if (d->noteStateMap[note] == 0) {
                        note->setOff();
                        d->noteStateMap.remove(note);
                    }
                }
            } else {
                if (setOn) {
                    note->setOn(velocity);
                    d->noteStateMap[note] = 1;
                } else {
                    note->setOff();
                }
            }
        }
    } else {
        qDebug() << "Attempted to set the state of a null-value note";
    }
}

QVariantList PlayGridManager::mostRecentlyChangedNotes() const
{
    return d->mostRecentlyChangedNotes;
}

void PlayGridManager::updateNoteState(QVariantMap metadata)
{
    static const QLatin1String note_on{"note_on"};
    static const QLatin1String note_off{"note_off"};
    int midiNote = metadata.value("note").toInt();
    int midiChannel = metadata.value("channel").toInt();
    const QString messageType = metadata.value("type").toString();
    if (messageType == note_on) {
        Note *note = d->findExistingNote(midiNote, midiChannel);
        note->setIsPlaying(true);
    } else if (messageType == note_off) {
        Note *note = d->findExistingNote(midiNote, midiChannel);
        note->setIsPlaying(false);
    }
    d->mostRecentlyChangedNotes << metadata;
    Q_EMIT mostRecentlyChangedNotesChanged();
}

void PlayGridManager::metronomeTick(int beat)
{
    if (beat % 32 == 0) {
        d->metronomeBeat4th = beat / 32;
        Q_EMIT metronomeBeat4thChanged();
    }
    if (beat % 16 == 0) {
        d->metronomeBeat8th = beat / 16;
        Q_EMIT metronomeBeat8thChanged();
    }
    if (beat % 8 == 0) {
        d->metronomeBeat16th = beat / 8;
        Q_EMIT metronomeBeat16thChanged();
    }
    if (beat % 4 == 0) {
        d->metronomeBeat32nd = beat / 4;
        Q_EMIT metronomeBeat32ndChanged();
    }
    if (beat % 2 == 0) {
        d->metronomeBeat64th = beat / 2;
        Q_EMIT metronomeBeat64thChanged();
    }

    d->metronomeBeat128th = beat;
    Q_EMIT metronomeBeat128thChanged();
}

int PlayGridManager::metronomeBeat4th() const
{
    return d->metronomeBeat4th;
}

int PlayGridManager::metronomeBeat8th() const
{
    return d->metronomeBeat8th;
}

int PlayGridManager::metronomeBeat16th() const
{
    return d->metronomeBeat16th;
}

int PlayGridManager::metronomeBeat32nd() const
{
    return d->metronomeBeat32nd;
}

int PlayGridManager::metronomeBeat64th() const
{
    return d->metronomeBeat64th;
}

int PlayGridManager::metronomeBeat128th() const
{
    return d->metronomeBeat128th;
}

void PlayGridManager::setSyncTimer(QObject* syncTimer)
{
    if (d->syncTimer != syncTimer) {
        if (d->syncTimer) {
            d->syncTimer->removeCallback(&timer_callback);
        }
        d->syncTimer = qobject_cast<SyncTimer*>(syncTimer);
        if (d->syncTimer) {
            d->syncTimer->addCallback(&timer_callback);
        }
        Q_EMIT syncTimerChanged();
    }
}

void PlayGridManager::setSyncTimerObj(int memoryAddress)
{
    qDebug() << Q_FUNC_INFO << "Setting sync timer object by explicitly interpreting an integer as a memory address. This isn't awesome, but it'll have to do for now. See todo in PlayGridManage.h";
    QObject* thing = reinterpret_cast<QObject*>(memoryAddress);
    setSyncTimer(thing);
}

QObject* PlayGridManager::syncTimer() const
{
    return d->syncTimer;
}

void PlayGridManager::startMetronome()
{
    // If we've already registered ourselves to get a callback, don't do that again, it just gets silly
    if (!timer_callback_tickers->contains(this)) {
        // TODO Send start metronome request to libzl
        timer_callback_tickers->append(this);
        Q_EMIT requestMetronomeStart();
    }
}

void PlayGridManager::stopMetronome()
{
    // TODO Send stop metronome request to libzl
    timer_callback_tickers->removeAll(this);
    Q_EMIT requestMetronomeStop();
    d->metronomeBeat4th = 0;
    d->metronomeBeat8th = 0;
    d->metronomeBeat16th = 0;
    d->metronomeBeat32nd = 0;
    d->metronomeBeat64th = 0;
    d->metronomeBeat128th = 0;
    Q_EMIT metronomeBeat4thChanged();
    Q_EMIT metronomeBeat8thChanged();
    Q_EMIT metronomeBeat16thChanged();
    Q_EMIT metronomeBeat32ndChanged();
    Q_EMIT metronomeBeat64thChanged();
    Q_EMIT metronomeBeat128thChanged();
}
