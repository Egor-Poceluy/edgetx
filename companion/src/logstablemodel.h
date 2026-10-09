/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include <QAbstractTableModel>
#include "logsdata.h"

class LogTableModel : public QAbstractTableModel
{
  Q_OBJECT

public:
  explicit LogTableModel(QObject* parent = 0)
    : QAbstractTableModel(parent) {}

  void setLog(const LogData& d) {
    beginResetModel();
    m_data = d;
    endResetModel();
  }
  void clear() {
    beginResetModel();
    m_data = LogData{};
    endResetModel();
  }

  const LogData& log() const { return m_data; }

  int rowCount(const QModelIndex& = QModelIndex()) const override {
    return m_data.rowCount();
  }
  int columnCount(const QModelIndex& = QModelIndex()) const override {
    return m_data.colCount();
  }

  QVariant data(const QModelIndex& idx, int role) const override {
    if (!idx.isValid() || role != Qt::DisplayRole) return {};
    return m_data.fields.at(idx.column()).valueAt(idx.row());
  }

  QVariant headerData(int section, Qt::Orientation o, int role) const override {
    if (role != Qt::DisplayRole) return {};
    if (o == Qt::Horizontal) return m_data.columnNames.value(section);
    return section + 1;
  }

private:
  LogData m_data;
};
