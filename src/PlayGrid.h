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

#ifndef PLAYGRID_H
#define PLAYGRID_H

#include <QQuickItem>

class PlayGrid : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* playGridManager READ playGridManager WRITE setPlayGridManager NOTIFY playGridManagerChanged)

    /**
     * \brief The human-facing name of your playgrid (shown anywhere the grid is referred to in the UI)
     *
     * You should not make this overly long or awkward, as it is used as the visual identifier by your user.
     * Clever is fine, but always remember: unique is not a selling point in its own right.
     */
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

    /**
     * \brief If defined, a model which should be used to display items in the Dashboard's Patterns column
     *
     * This model must have at least a role named "text", which is the display text shown as the
     * title for the listview's item on the Dashboard.
     *
     * You should consider using getNamedInstance for this model, to ensure that it is synchronised correctly
     * across PlayGrid instances.
     *
     * @see PlayGridManager::dashboardItemPicked
     * @see getNamedInstance()
     */
    Q_PROPERTY(QObject* dashboardModel READ dashboardModel WRITE setDashboardModel NOTIFY dashboardModelChanged)

    /**
     * \brief A way to set the pitch shift value (between -8192 and 8191, 0 being no shift)
     */
    Q_PROPERTY(int pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    /**
     * \brief A way to set the modulation value (between -127 and 127, with 0 being no modulation)
     */
    Q_PROPERTY(int modulation READ modulation WRITE setModulation NOTIFY modulationChanged)
    /**
     * \brief A number which changes from 1 through 4 every 4th beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat4th READ metronomeBeat4th NOTIFY metronomeBeat4thChanged)
    /**
     * \brief A number which changes from 1 through 8 every 8th beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat8th READ metronomeBeat8th NOTIFY metronomeBeat8thChanged)
    /**
     * \brief A number which changes from 1 through 16 every 16th beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat16th READ metronomeBeat16th NOTIFY metronomeBeat16thChanged)
    /**
     * \brief A number which changes from 1 through 32 every 32nd beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat32nd READ metronomeBeat32nd NOTIFY metronomeBeat32ndChanged)
    /**
     * \brief A number which changes from 1 through 64 every 64th beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat64th READ metronomeBeat64th NOTIFY metronomeBeat64thChanged)
    /**
     * \brief A number which changes from 1 through 128 every 128th beat when the metronome is running
     * @see startMetronome()
     * @see stopMetronome()
     */
    Q_PROPERTY(int metronomeBeat128th READ metronomeBeat128th NOTIFY metronomeBeat128thChanged)
public:
    explicit PlayGrid(QQuickItem *parent = nullptr);
    ~PlayGrid() override;

    /**
     * \brief The signal which you should use to perform initialisations of your playgrid
     *
     * This signal should be used for initialising your playgrid, in place of Component.onCompleted
     */
    Q_SIGNAL void initialize();

    /**
     * \brief Get a note object representing the midi note passed to it
     *
     * @param midiNote The midi note you want an object representation of
     * @return The note representing the specified midi note
     */
    Q_INVOKABLE QObject* getNote(int midiNote, int midiChannel = 0);
    /**
     * \brief Get a single note representing a single note
     *
     * @param notes A list of note objects
     * @return The single note representing all the notes passed to the function
     */
    Q_INVOKABLE QObject* getCompoundNote(const QVariantList &notes);
    /**
     * \brief Returns a model suitable for storing notes in
     *
     * Use this function to fetch a named model, which will persist for the duration of the application
     * session. What this means is that you can use this function to get a specific model that you have
     * previously created, and avoid having to refill it every time you need to show your playgrid. You
     * can thus fetch this model, and before attempting to fill it up, you can check whether there are
     * any notes in the model already, by using the `rows` property on it to check how many rows of
     * notes are currently in the model.
     *
     * @param modelName The name of the model
     * @return A model with the given name
     */
    Q_INVOKABLE QObject* getModel(const QString &modelName);

    /**
     * \brief Get a named instance of some QML type (newly created, or the same instance)
     * This will return the same instance for any named object you attempt to fetch. If an
     * object with that name was created by this function in the past, that instance will
     * be returned. If none exists already, a new instance will be created.
     * @note If the function was called with one type and later with another, it will
     * return an object of the type of the original call, rather than a new one of the new
     * type.
     * @note Unlike the similarly named function on PlayGridManager, this function will
     * namespace the object's name, by prepending it with the name of this playgrid. This
     * will be relevant if you introspect using objectName, which will then be
     * "Playgrid Name NameParameter" (note the space)
     * @param name The name of the object you want to retrieve
     * @param qmlTypeName The name of the QML object type you want an instance of
     * @return The instance with the given name
     */
    Q_INVOKABLE QObject* getNamedInstance(const QString &name, const QString& qmlTypeName);
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

    /**
     * \brief Turns the note passed to it on, if it is not already playing
     *
     * This will turn on the note, but it will not turn the note off and back on again if it is already
     * turned on. If you wish to release and fire the note again, you can either check the note's
     * isPlaying property first and then turn it off first, or you can simply call setNoteOff, and then
     * call setNoteOn immediately.
     *
     * @param note The note which should be turned on
     * @param velocity The velocity at which the note should be played (defaults to 64)
     */
    Q_INVOKABLE void setNoteOn(QObject *note, int velocity = 64);
    /**
     * \brief Turns the note passed to it off
     *
     * @param note The note which should be turned off
     */
    Q_INVOKABLE void setNoteOff(QObject *note);
    /**
     * \brief Turn a list of notes on, with the specified velocities
     *
     * @param notes A list of notes
     * @param velocities A list of velocities (must be equal length to notes)
     */
    Q_INVOKABLE void setNotesOn(const QVariantList &notes, const QVariantList &velocities);
    /**
     * \brief Turn a list of notes off
     *
     * @param notes A list of notes
     */
    Q_INVOKABLE void setNotesOff(const QVariantList &notes);

    /**
     * \brief Load a string value saved to disk under a specified name
     * @param key The name of the data you wish to retrieve
     * @return A string containing the data contained in the specified key (an empty string if none was found)
     */
    Q_INVOKABLE QString loadData(const QString &key);
    /**
     * \brief Save a string value to disk under a specified name
     *
     * @note The key will be turned into a filesystem-safe string before attempting to save the data
     *       to disk. This also means that if you are overly clever with naming, you may end up with
     *       naming clashes. In other words, be sensible in naming your keys, and your behaviour will
     *       be more predictable.
     * @param key The name of the data you wish to store
     * @param data The contents you wish to store, in string form
     * @return True if successful, false if unsuccessful
     */
    Q_INVOKABLE bool saveData(const QString &key, const QString &data);

    QObject *playGridManager() const;
    void setPlayGridManager(QObject *playGridManager);
    Q_SIGNAL void playGridManagerChanged();

    QString name() const;
    void setName(const QString &name);
    Q_SIGNAL void nameChanged();

    QObject *dashboardModel() const;
    void setDashboardModel(QObject *model);
    Q_SIGNAL void dashboardModelChanged();

    int pitch() const;
    void setPitch(int pitch);
    Q_SIGNAL void pitchChanged();

    int modulation() const;
    void setModulation(int modulation);
    Q_SIGNAL void modulationChanged();

    /**
     * \brief Start the system which provides beat updates
     *
     * Use the properties matching the beat division you need (metronome4thBeat and so on),
     * in which you will be told at which subdivision of the beat you are at. The counter
     * in the properties is 1-indexed, meaning you get numbers from 1 through the number of
     * the division.
     *
     * @see stopMetronome()
     */
    Q_INVOKABLE void startMetronome();
    /**
     * \brief Stop the beat updates being sent into the playgrid
     * @see startMetronome()
     */
    Q_INVOKABLE void stopMetronome();

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
private:
    class Private;
    Private *d;
};

#endif//PLAYGRID_H
