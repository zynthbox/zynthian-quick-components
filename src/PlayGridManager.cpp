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

#include <QQmlEngine>
#include <QDebug>

class PlayGridManager::Private
{
public:
    Private(PlayGridManager *q) : q(q) {}
    PlayGridManager *q;
    QStringList playgrids;
    QVariantMap currentPlaygrids;
    int pitch{0};
    int modulation{0};
    QMap<QString, NotesModel*> notesModels;
    QList<Note*> notes;
    QMap<QString, SettingsContainer*> settingsContainers;
    QMap<Note*, int> noteStateMap;
    int metronomeBeat4th{0};
    int metronomeBeat8th{0};
    int metronomeBeat16th{0};

    void updatePlaygrids()
    {
        QStringList newPlaygrids;
        if (playgrids != newPlaygrids) {
            playgrids = newPlaygrids;
            Q_EMIT q->playgridsChanged();
        }
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
            note->setSubnotes(actualNotes);
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
        QObjectList subnotes = note->subnotes();
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

void PlayGridManager::startMetronome()
{
    // TODO Send start metronome request to libzl
    // TODO connect to libzl timer signals
    Q_EMIT requestMetronomeStart();
}

void PlayGridManager::stopMetronome()
{
    // TODO disconnect from libzl timer signals
    // TODO Send stop metronome request to libzl
    Q_EMIT requestMetronomeStop();
    d->metronomeBeat4th = 0;
    d->metronomeBeat8th = 0;
    d->metronomeBeat16th = 0;
    Q_EMIT metronomeBeat4thChanged();
    Q_EMIT metronomeBeat8thChanged();
    Q_EMIT metronomeBeat16thChanged();
}
