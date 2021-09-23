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

#include "Note.h"

class Note::Private {
public:
    Private() {}
    PlayGridManager* playGridManager;
    QString name;
    int midiNote{0};
    int midiChannel{0};
    bool isPlaying{false};
    QVariantList subnotes;
};

Note::Note(PlayGridManager* parent)
    : QObject(parent)
    , d(new Private)
{
    d->playGridManager = parent;
}

Note::~Note()
{
    delete d;
}

void Note::setName(const QString& name)
{
    if (name != d->name) {
        d->name = name;
        Q_EMIT nameChanged();
    }
}

QString Note::name() const
{
    return d->name;
}

void Note::setMidiNote(int midiNote)
{
    if (midiNote != d->midiNote) {
        d->midiNote = midiNote;
        Q_EMIT midiNoteChanged();
    }
}

int Note::midiNote() const
{
    return d->midiNote;
}

int Note::octave() const
{
    return d->midiNote % 12;
}

void Note::setMidiChannel(int midiChannel)
{
    if (midiChannel != d->midiChannel) {
        d->midiChannel = midiChannel;
        Q_EMIT midiChannelChanged();
    }
}

int Note::midiChannel() const
{
    return d->midiChannel;
}

void Note::setIsPlaying(bool isPlaying)
{
    if (isPlaying != d->isPlaying) {
        d->isPlaying = isPlaying;
        Q_EMIT isPlayingChanged();
    }
}

bool Note::isPlaying() const
{
    return d->isPlaying;
}

void Note::setSubnotes(const QVariantList& subnotes)
{
    bool different = false;
    if (subnotes.count() == d->subnotes.count()) {
        for (int i = 0; i < subnotes.count(); ++i) {
            if (subnotes[i] != d->subnotes[i]) {
                different = true;
                break;
            }
        }
    } else {
        different = true;
    }
    if (different) {
        d->subnotes = subnotes;
        Q_EMIT subnotesChanged();
    }
}

QVariantList Note::subnotes() const
{
    return d->subnotes;
}

void Note::setOn(int velocity)
{
    Q_EMIT d->playGridManager->sendAMidiNoteMessage(d->midiNote, velocity, d->midiChannel, true);
}

void Note::setOff()
{
    Q_EMIT d->playGridManager->sendAMidiNoteMessage(d->midiNote, 0, d->midiChannel, false);
}
