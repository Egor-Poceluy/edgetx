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

#include "logs_table_model.h"

LogsTableModel::LogsTableModel(QObject* parent)
  : QAbstractTableModel(parent) {}


void LogsTableModel::set_log(const LogsData& data)
{
  beginResetModel();
  _data = data;
  endResetModel();
}


void LogsTableModel::clear()
{
  beginResetModel();
  _data = LogsData{};
  endResetModel();
}


const LogsData& LogsTableModel::log() const
{
  return _data;
}


int LogsTableModel::rowCount(const QModelIndex& parent) const
{
  Q_UNUSED(parent);
  return _data.row_count();
}


int LogsTableModel::columnCount(const QModelIndex& parent) const
{
  Q_UNUSED(parent);
  return _data.col_count();
}


QVariant LogsTableModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || role != Qt::DisplayRole) return {};
  return _data.fields.at(index.column()).value_at(index.row());
}


QVariant LogsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (role != Qt::DisplayRole) return {};
  if (orientation == Qt::Horizontal) return _data.column_names.value(section);
  return section + 1;
}