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
#include <QJsonObject>

class QQmlEngine;
class Note;
class PlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList playgrids READ playgrids NOTIFY playgridsChanged)
    Q_PROPERTY(QVariantMap currentPlaygrids READ currentPlaygrids NOTIFY currentPlaygridsChanged)
    Q_PROPERTY(QVariantMap dashboardModels READ dashboardModels NOTIFY dashboardModelsChanged)
    Q_PROPERTY(int pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(int modulation READ modulation WRITE setModulation NOTIFY modulationChanged)
    Q_PROPERTY(QVariantList mostRecentlyChangedNotes READ mostRecentlyChangedNotes NOTIFY mostRecentlyChangedNotesChanged)

    Q_PROPERTY(QObject* syncTimer READ syncTimer WRITE setSyncTimer NOTIFY syncTimerChanged)
    Q_PROPERTY(bool metronomeActive READ metronomeActive NOTIFY metronomeActiveChanged)
    Q_PROPERTY(int metronomeBeat4th READ metronomeBeat4th NOTIFY metronomeBeat4thChanged)
    Q_PROPERTY(int metronomeBeat8th READ metronomeBeat8th NOTIFY metronomeBeat8thChanged)
    Q_PROPERTY(int metronomeBeat16th READ metronomeBeat16th NOTIFY metronomeBeat16thChanged)
    Q_PROPERTY(int metronomeBeat32nd READ metronomeBeat32nd NOTIFY metronomeBeat32ndChanged)
    Q_PROPERTY(int metronomeBeat64th READ metronomeBeat64th NOTIFY metronomeBeat64thChanged)
    Q_PROPERTY(int metronomeBeat128th READ metronomeBeat128th NOTIFY metronomeBeat128thChanged)
public:
    explicit PlayGridManager(QQmlEngine *parent = nullptr);
    ~PlayGridManager() override;

    QStringList playgrids() const;
    Q_SIGNAL void playgridsChanged();
    Q_INVOKABLE void updatePlaygrids();

    QVariantMap currentPlaygrids() const;
    Q_SIGNAL void currentPlaygridsChanged();
    Q_INVOKABLE void setCurrentPlaygrid(const QString &section, int index);

    QVariantMap dashboardModels() const;
    Q_SIGNAL void dashboardModelsChanged();
    Q_INVOKABLE void pickDashboardModelItem(QObject* model, int index);
    Q_SIGNAL void dashboardItemPicked(QObject* model, int index);
    void registerDashboardModel(const QString &playgrid, QObject* model);

    int pitch() const;
    void setPitch(int pitch);
    Q_SIGNAL void pitchChanged();

    int modulation() const;
    void setModulation(int modulation);
    Q_SIGNAL void modulationChanged();

    /**
     * \brief Returns a sequence model suitable for holding a series of PatternModel instances
     *
     * Use this function to fetch a named model, which will persist for the duration of the application
     * session.
     * @note If passed an empty string, this will return the global sequence model
     * @param name The name of the sequence (pass an empty string, or "Global", to fetch the global sequence)
     * @return A Sequence with the given name (or the global sequence if passed an empty name)
     */
    Q_INVOKABLE QObject* getSequenceModel(const QString &name);
    /**
     * \brief Returns a model suitable for use as a pattern
     *
     * Use this function to fetch a named model, which will persist for the duration of the application
     * session. What this means is that you can use this function to get a specific model that you have
     * previously created, and avoid having to refill it every time you need to show your playgrid. You
     * can thus fetch this model, and before attempting to fill it up, you can check whether it contains
     * any notes, by using the "isEmpty" function, and only then load data into it.
     *
     * @note This will return the global pattern with the given name. If you need a playgrid-local one, see PlayGrid::getPattern
     * @param patternName The name of the pattern
     * @param sequenceName The name of the sequence the pattern is associated with (pass an empty string or "Global" for the global sequence, see getSequenceModel)
     * @return The pattern with the given name
     */
    Q_INVOKABLE QObject* getPatternModel(const QString &name, const QString& sequenceName);
    Q_INVOKABLE QObject* getNotesModel(const QString &name);
    Q_INVOKABLE QObject* getNote(int midiNote, int midiChannel = 0);
    Q_INVOKABLE QObject* getCompoundNote(const QVariantList &notes);
    Q_INVOKABLE QObject* getSettingsStore(const QString &name);
    /**
     * \brief Get a named instance of some QML type (newly created, or the same instance)
     * This will return the same instance for any named object you attempt to fetch. If an
     * object with that name was created by this function in the past, that instance will
     * be returned. If none exists already, a new instance will be created.
     * @note If the function was called with one type and later with another, it will
     * return an object of the type of the original call, rather than a new one of the new
     * type.
     * @note Unlike the similarly named function on PlayGrid, this one interprets the name
     * verbatim (that is, it does not add any namespacing stuff to the name)
     * @param name The name of the object you want to retrieve
     * @param qmlTypeName The name of the QML object type you want an instance of
     * @return The instance with the given name
     */
    Q_INVOKABLE QObject* getNamedInstance(const QString &name, const QString& qmlTypeName);

    /**
     * \brief Get a JSON representation of a Note object
     * @param note The note you want a JSON representation of
     * @return The JSON object representing the note passed to the function
     */
    QJsonObject noteToJsonObject(Note *note) const;
    /**
     * \brief Get a Note object equivalent to the one stored in the passed-in JSON object
     * @param jsonObject The JSON object which should be converted to a Note instance
     * @return A Note object (if the json contained a valid representation of a Note), or null
     */
    Note *jsonObjectToNote(const QJsonObject &jsonObject);

    /**
     * \brief Get a JSON representation of the given model
     *
     * @param model A NotesModel object
     * @return A string containing a representation of the model's notes in JSON form
     */
    Q_INVOKABLE QString modelToJson(QObject *model) const;
    /**
     * \brief Set the contents of the given model based on the given JSON representation
     *
     * @param model A NotesModel object to set to match the json structure
     * @param json A string containing a JSON formatted representation of a model's contents (or a list, see notesListToJson())
     */
    Q_INVOKABLE void setModelFromJson(QObject *model, const QString &json);
    /**
     * \brief Get a JSON representation of a list of Note objects
     *
     * Given a list of Note objects (empty entries are allowed in this list), you can use this function
     * to get a JSON representation which you can save using for example saveData()
     *
     * @param notes A list of Note objects to get a JSON representation of
     * @return A JSON formatted string representing the list of notes
     */
    Q_INVOKABLE QString notesListToJson(const QVariantList &notes) const;
    /**
     * \brief Get a list of notes based on a JSON representation (may contain null notes)
     *
     * @param json A JSON formatted representation of notes (empty fields allowed)
     * @return A list of Note objects (or empty positions), or an empty list if invalid
     */
    Q_INVOKABLE QVariantList jsonToNotesList(const QString &json);
    /**
     * \brief Get a JSON representation of a single Note object
     *
     * @param note The Note object you wish to get a JSON representation of
     * @return A JSON string representation of the Note object
     */
    Q_INVOKABLE QString noteToJson(QObject *note) const;
    /**
     * \brief Get the Note object represented by the given JSON string (may return null)
     *
     * @param json A JSON representation of a Note
     * @return The Note object represented by the JSON passed to the function (or null if invalid)
     */
    Q_INVOKABLE QObject* jsonToNote(const QString &json);

    Q_INVOKABLE void setNotesOn(QVariantList notes, QVariantList velocities);
    Q_INVOKABLE void setNotesOff(QVariantList notes);
    Q_INVOKABLE void setNoteOn(QObject *note, int velocity = 64);
    Q_INVOKABLE void setNoteOff(QObject *note);

    Q_INVOKABLE void setNoteState(Note *note, int velocity = 64, bool setOn = true);
    Q_SIGNAL void noteStateChanged(QObject *note);
    Q_INVOKABLE QVariantList mostRecentlyChangedNotes() const;
    Q_SIGNAL void mostRecentlyChangedNotesChanged();
    Q_INVOKABLE void updateNoteState(QVariantMap metadata);

    /**
     * \brief Schedules a note to be set on or off on the next tick of the metronome
     * @param midiNote The note you wish to change the state of
     * @param midiChannel The channel you wish to change the given note on
     * @param setOn Whether or not you are turning the note on
     * @param velocity The velocity of the note (only matters if you're turning it on)
     * @param duration An optional duration (0 means don't schedule a release)
     * @param delay A delay in ms counting from the beat
     */
    Q_INVOKABLE void scheduleNote(int midiNote, int midiChannel, bool setOn = true, int velocity = 64, int duration = 0, int delay = 0);

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
    bool metronomeActive() const;
    Q_SIGNAL void metronomeActiveChanged();
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
    Q_INVOKABLE void sendAMidiNoteMessage(unsigned char midiNote, unsigned char velocity, int channel, bool setOn);
private:
    class Private;
    Private *d;
};

#endif//PLAYGRIDMANAGER_H
