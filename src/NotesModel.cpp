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

#include "NotesModel.h"
#include "Note.h"

#include <QTimer>

class NotesModel::Private {
public:
    Private(NotesModel *q)
        : q(q)
    {
        noteDataChangedUpdater.setInterval(1);
        noteDataChangedUpdater.setSingleShot(true);
        QObject::connect(&noteDataChangedUpdater, &QTimer::timeout, q, [this,q](){
            for (const QObjectList &list : entries) {
                for (QObject *obj : list) {
                    Note *note = qobject_cast<Note*>(obj);
                    note->disconnect(q);
                    connect(note, &Note::nameChanged, q, [this,note](){ emitNoteDataChanged(note); });
                    connect(note, &Note::midiNoteChanged, q, [this,note](){ emitNoteDataChanged(note); });
                    connect(note, &Note::midiChannelChanged, q, [this,note](){ emitNoteDataChanged(note); });
                    connect(note, &Note::isPlayingChanged, q, [this,note](){ emitNoteDataChanged(note); });
                    connect(note, &Note::subnotesChanged, q, [this,note](){ emitNoteDataChanged(note); });
                }
            }
        });
    }
    NotesModel *q;
    QList<QObjectList> entries;

    QTimer noteDataChangedUpdater;
    void emitNoteDataChanged(Note* note) {
        for (int row = 0; row < entries.count(); ++row) {
            QObjectList rowEntries = entries[row];
            for (int column = 0; column < rowEntries.count(); ++column) {
                if (rowEntries[column] == note) {
                    QModelIndex idx = q->index(row, column);
                    q->dataChanged(idx, idx);
                }
            }
        }
    }
};

NotesModel::NotesModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
}

NotesModel::~NotesModel()
{
    delete d;
}

QVariantMap NotesModel::roles() const
{
    static const QVariantMap roles{
        {"note", NoteRole}
    };
    return roles;
}

QHash<int, QByteArray> NotesModel::roleNames() const
{
    static const QHash<int, QByteArray> roles{
        {NoteRole, "note"}
    };
    return roles;
}

int NotesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return d->entries.count();
}

int NotesModel::columnCount(const QModelIndex& parent) const
{
    int count = 0;
    if (parent.isValid() && parent.row() >= 0 && parent.row() < d->entries.count()) {
        count = d->entries.at(parent.row()).count();
    }
    return count;
}

QVariant NotesModel::data(const QModelIndex& index, int role) const
{
    QVariant result;
    if (checkIndex(index)) {
        Note* note = qobject_cast<Note*>(d->entries.at(index.row()).at(index.column()));
        if (note) {
            switch(role) {
                case NoteRole:
                    result.setValue<QObject*>(note);
                    break;
                default:
                    break;
            }
        }
    }
    return result;
}

QModelIndex NotesModel::index(int row, int column, const QModelIndex& /*parent*/) const
{
    QModelIndex idx;
    if (row >= 0 && row < d->entries.count()) {
        if (column >= 0 && column < d->entries[row].count()) {
            idx = createIndex(row, column);
        }
    }
    return idx;
}

void NotesModel::clear()
{
    beginResetModel();
    for (QObjectList &list : d->entries) {
        for (QObject *obj : list) {
            obj->disconnect(this);
        }
    }
    d->entries.clear();
    endResetModel();
    Q_EMIT rowsChanged();
}

void NotesModel::addRow(const QVariantList &notes)
{
    QObjectList actualNotes;
    for (const QVariant &var : notes) {
        actualNotes << var.value<QObject*>();
    }
    if (actualNotes.count() > 0) {
        int newRow = d->entries.count();
        beginInsertRows(QModelIndex(), newRow, newRow);
        d->entries.append(actualNotes);
        d->noteDataChangedUpdater.start();
        endInsertRows();
        Q_EMIT rowsChanged();
    }
}
