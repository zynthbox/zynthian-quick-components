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

#include "SequenceModel.h"
#include "PatternModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>

class SequenceModel::Private {
public:
    Private(SequenceModel *q)
        : q(q)
    { }
    SequenceModel *q;
    PlayGridManager *playGridManager{nullptr};
    QList<PatternModel*> patternModels;
    int activePattern{0};
    int version{0};

    QString getDataLocation()
    {
        QStringList keepcharacters{" ",".","_"};
        QString safe;
        for (const QChar &letter : q->objectName()) {
            if (letter.isLetterOrNumber() || keepcharacters.contains(letter)) {
                safe.append(letter);
            }
        }
        // test and make sure that this env var contains something, or spit out .local/zynthian or something
        return QString("%1/session/sequences/%2").arg(QString(qgetenv("ZYNTHIAN_MY_DATA_DIR"))).arg(safe);
    }
};

SequenceModel::SequenceModel(PlayGridManager* parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
    d->playGridManager = parent;
}

SequenceModel::~SequenceModel()
{
    delete d;
}

QHash<int, QByteArray> SequenceModel::roleNames() const
{
    static const QHash<int, QByteArray> roles{
        {PatternRole, "pattern"},
        {NameRole, "name"},
    };
    return roles;
}

QVariant SequenceModel::data(const QModelIndex& index, int role) const
{
    QVariant result;
    if (checkIndex(index)) {
        PatternModel *model = d->patternModels.at(index.row());
        switch (role) {
        case PatternRole:
            result.setValue<QObject*>(model);
            break;
        case NameRole:
            result.setValue(model->objectName());
            break;
        }
    }
    return result;
}

int SequenceModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return d->patternModels.count();
}

QModelIndex SequenceModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return createIndex(row, column);
}

void SequenceModel::insertPattern(PatternModel* pattern, int row)
{
    int insertionRow = qMax(0, qMin(d->patternModels.count(), row));
    beginInsertRows(QModelIndex(), insertionRow, insertionRow);
    d->patternModels.insert(insertionRow, pattern);
    endInsertRows();
}

void SequenceModel::removePattern(PatternModel* pattern)
{
    int removalPosition = d->patternModels.indexOf(pattern);
    if (removalPosition > -1) {
        beginRemoveRows(QModelIndex(), removalPosition, removalPosition);
        d->patternModels.removeAt(removalPosition);
        endRemoveRows();
    }
}

bool SequenceModel::contains(QObject* pattern)
{
    return d->patternModels.contains(qobject_cast<PatternModel*>(pattern));
}

PlayGridManager* SequenceModel::playGridManager() const
{
    return d->playGridManager;
}

void SequenceModel::setActivePattern(int activePattern)
{
    if (d->activePattern != activePattern) {
        d->activePattern = activePattern;
        Q_EMIT activePatternChanged();
    }
}

int SequenceModel::activePattern() const
{
    return d->activePattern;
}

void SequenceModel::load()
{
    QString data;
    QFile file(d->getDataLocation() + "/" + QString::number(d->version));
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            data = QString::fromUtf8(file.readAll());
            file.close();
        }
    }
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data.toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject obj = jsonDoc.object();
        beginResetModel();
        d->patternModels.clear();
        int patternNumber{0};
        for (const QJsonValue &patternValue : obj.value("patterns").toArray()) {
            ++patternNumber;
            PatternModel *model = qobject_cast<PatternModel*>(playGridManager()->getPatternModel(QString("Pattern ").arg(QString::number(patternNumber)), objectName()));
            playGridManager()->setModelFromJson(model, patternValue.toString());
        }
        setActivePattern(obj.value("activePattern").toInt());
        endResetModel();
    }
}

bool SequenceModel::save()
{
    bool success = false;
    QString data;
    if (data.isEmpty()) {
        QDir confLocation(d->getDataLocation());
        if (confLocation.exists() || confLocation.mkpath(confLocation.path())) {
            QFile dataFile(confLocation.path() + "/" + QString::number(d->version));
            if (dataFile.open(QIODevice::WriteOnly) && dataFile.write(data.toUtf8())) {
                success = true;
            }
        }
    }
    return success;
}
