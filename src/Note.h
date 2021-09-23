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

#ifndef NOTE_H
#define NOTE_H

#include <QObject>
#include "PlayGridManager.h"

class Note : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(int midiNote READ midiNote NOTIFY midiNoteChanged)
    Q_PROPERTY(int midiChannel READ midiChannel NOTIFY midiChannelChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(QObjectList subnotes READ subnotes WRITE setSubnotes NOTIFY subnotesChanged)
public:
    explicit Note(PlayGridManager *parent = nullptr);
    ~Note() override;

    void setName(const QString& name);
    QString name() const;
    Q_SIGNAL void nameChanged();

    void setMidiNote(int midiNote);
    int midiNote() const;
    Q_SIGNAL void midiNoteChanged();

    void setMidiChannel(int midiChannel);
    int midiChannel() const;
    Q_SIGNAL void midiChannelChanged();

    void setIsPlaying(bool isPlaying);
    bool isPlaying() const;
    Q_SIGNAL void isPlayingChanged();

    void setSubnotes(const QObjectList& subnotes);
    QObjectList subnotes() const;
    Q_SIGNAL void subnotesChanged();

    Q_INVOKABLE void setOn(int velocity = 64);
    Q_INVOKABLE void setOff();
private:
    class Private;
    Private* d;
};

#endif//NOTE_H
