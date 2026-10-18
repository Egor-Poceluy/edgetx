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

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVarLengthArray>
#include <QMetaType>
#include <QVariant>

#define INVALID_MIN 999999
#define INVALID_MAX -999999

enum yaxes_t {
  firstLeft = 0,
  firstRight,
  secondLeft,
  secondRight,
  AXES_LIMIT
};

struct coords_t {
  QVector<double> x, y;
  double min_y, max_y;
  yaxes_t yaxis;
  QString name;
};

struct minMax_t { double min, max; };

struct plotsCollection {
  QVarLengthArray<coords_t> coords;
  double min_x, max_x;
  bool too_many_ranges;
  minMax_t y_axes_ranges[AXES_LIMIT];
  double y_axes_ratios[AXES_LIMIT];
  double common_min_y, common_max_y;
};

struct LogField {
  QString name;
  bool is_numeric;
  QVector<double> numbers;
  QVector<QString> strings;

  QVariant value_at(int index) const {
    if (is_numeric) return numbers.at(index);
    return strings.at(index);
  }
};

struct LogsData {
  QString filename;
  QStringList column_names;
  QVector<LogField> fields;
  QVector<qint64> timestamps;
  QList<int> session_breaks;
  int errors = 0;
  int lines = 0;

  int row_count() const { return timestamps.size(); }
  int col_count() const { return fields.size(); }
  bool is_empty() const { return timestamps.isEmpty(); }
};

struct PlotRequest {
  QList<int> selected_columns;
  QList<int> selected_rows;
  QStringList field_names;
  bool use_common_axes = false;
};

Q_DECLARE_METATYPE(coords_t)
Q_DECLARE_METATYPE(plotsCollection)
Q_DECLARE_METATYPE(LogsData)
Q_DECLARE_METATYPE(PlotRequest)