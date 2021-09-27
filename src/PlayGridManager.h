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

#ifndef PLAYGRIDMANAGER_H
#define PLAYGRIDMANAGER_H

#include <QObject>
#include <QVariantMap>

class Note;
class PlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList playgrids READ playgrids NOTIFY playgridsChanged)
    Q_PROPERTY(QVariantMap currentPlaygrids READ currentPlaygrids NOTIFY currentPlaygridsChanged)
    Q_PROPERTY(int pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(int modulation READ modulation WRITE setModulation NOTIFY modulationChanged)
    Q_PROPERTY(QVariantList mostRecentlyChangedNotes READ mostRecentlyChangedNotes NOTIFY mostRecentlyChangedNotesChanged)

    Q_PROPERTY(QObject* syncTimer READ syncTimer WRITE setSyncTimer NOTIFY syncTimerChanged)
    Q_PROPERTY(int metronomeBeat4th READ metronomeBeat4th NOTIFY metronomeBeat4thChanged)
    Q_PROPERTY(int metronomeBeat8th READ metronomeBeat8th NOTIFY metronomeBeat8thChanged)
    Q_PROPERTY(int metronomeBeat16th READ metronomeBeat16th NOTIFY metronomeBeat16thChanged)
    Q_PROPERTY(int metronomeBeat32nd READ metronomeBeat32nd NOTIFY metronomeBeat32ndChanged)
    Q_PROPERTY(int metronomeBeat64th READ metronomeBeat64th NOTIFY metronomeBeat64thChanged)
    Q_PROPERTY(int metronomeBeat128th READ metronomeBeat128th NOTIFY metronomeBeat128thChanged)
public:
    explicit PlayGridManager(QObject *parent = nullptr);
    ~PlayGridManager() override;

    QStringList playgrids() const;
    Q_SIGNAL void playgridsChanged();
    Q_INVOKABLE void updatePlaygrids();

    QVariantMap currentPlaygrids() const;
    Q_SIGNAL void currentPlaygridsChanged();
    Q_INVOKABLE void setCurrentPlaygrid(const QString &section, int index);

    int pitch() const;
    void setPitch(int pitch);
    Q_SIGNAL void pitchChanged();

    int modulation() const;
    void setModulation(int modulation);
    Q_SIGNAL void modulationChanged();

    Q_INVOKABLE QObject* getNotesModel(const QString &name);
    Q_INVOKABLE QObject* getNote(int midiNote, int midiChannel = 0);
    Q_INVOKABLE QObject* getCompoundNote(const QVariantList &notes);
    Q_INVOKABLE QObject* getSettingsStore(const QString &name);

    Q_INVOKABLE void setNotesOn(QVariantList notes, QVariantList velocities);
    Q_INVOKABLE void setNotesOff(QVariantList notes);
    Q_INVOKABLE void setNoteOn(QObject *note, int velocity = 64);
    Q_INVOKABLE void setNoteOff(QObject *note);

    Q_INVOKABLE void setNoteState(Note *note, int velocity = 64, bool setOn = true);
    Q_SIGNAL void noteStateChanged(QObject *note);
    Q_INVOKABLE QVariantList mostRecentlyChangedNotes() const;
    Q_SIGNAL void mostRecentlyChangedNotesChanged();
    Q_INVOKABLE void updateNoteState(QVariantMap metadata);

    QObject *syncTimer() const;
    void setSyncTimer(QObject *syncTimer);
    // TODO When someone works out how to pass a c_void_p through python and set it as the value
    // of a QObject* property, the following function needs to go very away.
    // This is a reeeally ugly bit of something-something-something, but until someone can fix
    // up the libzl passthrough in python, this works...
    Q_INVOKABLE void setSyncTimerObj(int memoryAddress);
    Q_SIGNAL void syncTimerChanged();

    Q_INVOKABLE void startMetronome();
    // TODO This is a temporary thing while we get the c++ side integrated properly
    Q_SIGNAL void requestMetronomeStart();
    Q_INVOKABLE void stopMetronome();
    // TODO This is a temporary thing while we get the c++ side integrated properly
    Q_SIGNAL void requestMetronomeStop();

    Q_INVOKABLE void metronomeTick(int beat);
    int metronomeBeat4th() const;
    Q_SIGNAL void metronomeBeat4thChanged();
    int metronomeBeat8th() const;
    Q_SIGNAL void metronomeBeat8thChanged();
    int metronomeBeat16th() const;
    Q_SIGNAL void metronomeBeat16thChanged();
    int metronomeBeat32nd() const;
    Q_SIGNAL void metronomeBeat32ndChanged();
    int metronomeBeat64th() const;
    Q_SIGNAL void metronomeBeat64thChanged();
    int metronomeBeat128th() const;
    Q_SIGNAL void metronomeBeat128thChanged();

    // TODO This is a temporary thing while we get the c++ side integrated properly
    Q_SIGNAL void sendAMidiNoteMessage(int midiNote, int velocity, int channel, bool setOn);
private:
    class Private;
    Private *d;
};

#endif//PLAYGRIDMANAGER_H
