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

#ifndef SEQUENCEMODEL_H
#define SEQUENCEMODEL_H

#include <QAbstractListModel>
#include "PlayGridManager.h"

class PatternModel;
class SequenceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int activePattern READ activePattern WRITE setActivePattern NOTIFY activePatternChanged)
    Q_PROPERTY(QObject* activePatternObject READ activePatternObject NOTIFY activePatternChanged)
public:
    explicit SequenceModel(PlayGridManager *parent = nullptr);
    ~SequenceModel() override;

    enum Roles {
        PatternRole = Qt::UserRole + 1,
        TextRole,
        NameRole,
        LayerRole,
    };

    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Q_INVOKABLE QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;

    /**
     * \brief Get the pattern object for the given position (or null if none exists)
     * @param patternIndex The index of the pattern to fetch from the model
     * @return The PatternModel at the given index, or null
     */
    Q_INVOKABLE QObject *get(int patternIndex) const;
    /**
     * \brief Insert a pattern into the sequence at the desired location (or at the end if undefined)
     * @param pattern The pattern to insert into the model
     * @param row The row at which the pattern should be inserted (if -1, it will be added at the end of the model)
     */
    Q_INVOKABLE void insertPattern(PatternModel *pattern, int row = -1);
    /**
     * \brief Remove a pattern from the sequence
     * @param pattern The pattern that should be removed from the model
     */
    Q_INVOKABLE void removePattern(PatternModel *pattern);
    /**
     * \brief Check whether a pattern already exists in this sequence
     * @return True if the pattern is already in the sequence, false if not
     */
    Q_INVOKABLE bool contains(QObject *pattern);

    PlayGridManager *playGridManager() const;

    void setActivePattern(int activePattern);
    int activePattern() const;
    QObject *activePatternObject() const;
    Q_SIGNAL void activePatternChanged();

    /**
     * \brief Load the data for this Sequence (and all Patterns contained within it) from disk
     */
    Q_INVOKABLE void load();
    /**
     * \brief Save the data for this Sequence (and all Patterns contained within it) to disk
     * @return True if successful, false if not
     */
    Q_INVOKABLE bool save();
    /**
     * \brief Clear all patterns of all notes
     */
    Q_INVOKABLE void clear();

    /**
     * \brief Set the named property on the pattern with the specified index the given value
     * @param patternIndex The index in the list of the pattern to set a property on
     * @param property The name of the property you wish to change the value of
     * @param value The value you wish to set that property to
     */
    Q_INVOKABLE void setPatternProperty(int patternIndex, const QString &property, const QVariant &value);

    /**
     * \brief Set any note previously turned on using setPositionOn off
     */
    Q_INVOKABLE void setPreviousOff() const;
    /**
     * \brief Set the given position on in all enabled patterns which have the position, and turn all previous off (optionally leave them on)
     * @param row The row you wish to set to on in all enabled patterns (bankOffset will be interpreted by this function per-pattern)
     * @param column The column in the given row you wish to turn on in all enabled patterns
     * @param stopPrevious If true, the function will stop any previous notes set on on the same sequence (default is to do so, pass false to disable this automagic)
     */
    Q_INVOKABLE void setPositionOn(int row, int column, bool stopPrevious = true) const;
private:
    class Private;
    Private *d;
};

#endif//SEQUENCEMODEL_H
