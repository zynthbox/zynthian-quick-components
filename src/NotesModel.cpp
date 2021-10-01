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

struct Entry {
    Note* note{nullptr};
    QVariant metaData;
};

class NotesModel::Private {
public:
    Private(NotesModel *q)
        : q(q)
    {
        noteDataChangedUpdater.setInterval(1);
        noteDataChangedUpdater.setSingleShot(true);
        QObject::connect(&noteDataChangedUpdater, &QTimer::timeout, q, [this,q](){
            for (const QList<Entry> &list : entries) {
                for (const Entry &obj : list) {
                    Note *note = obj.note;
                    if (note) {
                        note->disconnect(q);
                        connect(note, &Note::nameChanged, q, [this,note](){ emitNoteDataChanged(note); });
                        connect(note, &Note::midiNoteChanged, q, [this,note](){ emitNoteDataChanged(note); });
                        connect(note, &Note::midiChannelChanged, q, [this,note](){ emitNoteDataChanged(note); });
                        connect(note, &Note::isPlayingChanged, q, [this,note](){ emitNoteDataChanged(note); });
                        connect(note, &Note::subnotesChanged, q, [this,note](){ emitNoteDataChanged(note); });
                    }
                }
            }
        });
    }
    NotesModel *q;
    QList< QList<Entry> > entries;

    void ensurePositionExists(int row, int column) {
        if (entries.count() < row - 1) {
            q->beginInsertRows(QModelIndex(), entries.count(), row);
            for (int i = entries.count() - 1; i < row + 1; ++i) {
                entries << QList<Entry>();
            }
            q->endInsertRows();
        }
        QList<Entry> rowList = entries[row];
        if (rowList.count() < column + 1) {
            q->beginInsertColumns(QModelIndex(), rowList.count(), column + 1);
            for (int i = rowList.count() - 1; i < column + 1; ++i) {
                rowList << Entry();
            }
            q->endInsertColumns();
        }
        entries[row] = rowList;
    }
    QTimer noteDataChangedUpdater;
    void emitNoteDataChanged(Note* note) {
        for (int row = 0; row < entries.count(); ++row) {
            QList<Entry> rowEntries = entries[row];
            for (int column = 0; column < rowEntries.count(); ++column) {
                const Entry &entry = rowEntries.at(column);
                if (entry.note == note) {
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
        {"note", NoteRole},
        {"metadata", MetadataRole}
    };
    return roles;
}

QHash<int, QByteArray> NotesModel::roleNames() const
{
    static const QHash<int, QByteArray> roles{
        {NoteRole, "note"},
        {MetadataRole, "metadata"}
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
    if (index.row() >= 0 && index.row() < d->entries.count()) {
        QList<Entry> rowEntries = d->entries.at(index.row());
        if (index.column() >= 0 && index.column() < rowEntries.count()) {
            const Entry &entry = rowEntries.at(index.column());
            switch(role) {
            case NoteRole:
                result.setValue<QObject*>(entry.note);
                break;
            case MetadataRole:
                result = entry.metaData;
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

QVariantList NotesModel::getRow(int row) const
{
    QVariantList list;
    if (row >= 0 && row < d->entries.count()) {
        const QList<Entry> &entries = d->entries.at(row);
        for (const Entry &entry : entries) {
            list << QVariant::fromValue<QObject*>(entry.note);
        }
    }
    return list;
}

QObject* NotesModel::getNote(int row, int column) const
{
    QObject *obj{nullptr};
    if (row >= 0 && row < d->entries.count()) {
        QList<Entry> rowEntries = d->entries.at(row);
        if (column >= 0 && column < rowEntries.count()) {
            obj = rowEntries.at(column).note;
        }
    }
    return obj;
}

void NotesModel::setNote(int row, int column, QObject* note)
{
    d->ensurePositionExists(row, column);
    QList<Entry> rowList = d->entries[row];
    rowList[column].note = qobject_cast<Note*>(note);
    d->entries[row] = rowList;
    QModelIndex changed = createIndex(row, column);
    dataChanged(changed, changed);
}

QVariantList NotesModel::getRowMetadata(int row) const
{
    QVariantList list;
    if (row >= 0 && row < d->entries.count()) {
        const QList<Entry> &entries = d->entries.at(row);
        for (const Entry &entry : entries) {
            list << entry.metaData;
        }
    }
    return list;
}

QVariant NotesModel::getMetadata(int row, int column) const
{
    QVariant data;
    if (row >= 0 && row < d->entries.count()) {
        QList<Entry> rowEntries = d->entries.at(row);
        if (column >= 0 && column < rowEntries.count()) {
            data = rowEntries.at(column).metaData;
        }
    }
    return data;
}

void NotesModel::setMetadata(int row, int column, QVariant metadata)
{
    d->ensurePositionExists(row, column);
    QList<Entry> rowList = d->entries[row];
    rowList[column].metaData = metadata;
    d->entries[row] = rowList;
    QModelIndex changed = createIndex(row, column);
    dataChanged(changed, changed);
}

void NotesModel::trim()
{
    QList< QList<Entry> > newList;
    for (const QList<Entry> &rowList : d->entries) {
        QList<Entry> newRow;
        QList<Entry> trailing;
        for (const Entry& entry : rowList) {
            if (entry.note) {
                newRow << trailing;
                trailing.clear();
                newRow << entry;
            } else {
                trailing << entry;
            }
        }
        if (newRow.count() > 0) {
            newList << newRow;
        }
    }
    beginResetModel();
    d->entries = newList;
    endResetModel();
}

void NotesModel::clear()
{
    beginResetModel();
    for (QList<Entry> &list : d->entries) {
        for (const Entry &entry : list) {
            if (entry.note) {
                entry.note->disconnect(this);
            }
        }
    }
    d->entries.clear();
    endResetModel();
    Q_EMIT rowsChanged();
}

void NotesModel::addRow(const QVariantList &notes)
{
    QList<Entry> actualNotes;
    for (const QVariant &var : notes) {
        Note *note = qobject_cast<Note*>(var.value<QObject*>());
        Entry entry;
        entry.note = note;
        actualNotes << entry;
    }
    if (actualNotes.count() > 0) {
        beginInsertRows(QModelIndex(), 0, 0);
        d->entries.insert(0, actualNotes);
        d->noteDataChangedUpdater.start();
        endInsertRows();
        Q_EMIT rowsChanged();
    }
}
