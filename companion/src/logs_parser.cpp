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

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include "logs_parser.h"

namespace LogsParser {

static bool looks_numeric(const QString& str)
{
  if (str.isEmpty()) {
    return false;
  }

  bool ok = false;
  str.toDouble(&ok);
  return ok;
}


Result parse_file(const QString& filename,
                  std::function<void(int)> progress_func,
                  std::function<bool()> cancel_check_func)
{
  Result result;

  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    result.error = QString("Cannot open file: %1").arg(filename);
    return result;
  }

  QString header = file.readLine();
  if (!header.startsWith("Date,Time")) {
    result.error = QString("Invalid format: %1").arg(filename);
    return result;
  }
  result.data.column_names = header.trimmed().split(',');

  int num_fields = result.data.column_names.size();
  result.data.fields.resize(num_fields);
  for (int i = 0; i < num_fields; ++i) {
    result.data.fields[i].name = result.data.column_names.at(i);
  }

  bool types_resolved = false;
  qint64 file_size = file.size();
  int errors = 0;
  int lines = -1;

  while (!file.atEnd()) {
    if (lines % 1024 == 0 && cancel_check_func && cancel_check_func()) {
      result.error = QString("cancelled");
      return result;
    }

    QString line = file.readLine().trimmed();
    if (line.isEmpty()) continue;

    QStringList cols = line.split(',');
    if (cols.size() != num_fields) {
      errors++;
      lines++;
      continue;
    }

    if (!types_resolved) {
      for (int i = 0; i < num_fields; ++i) {
        if (i < 2) {
          result.data.fields[i].is_numeric = false;
        }
        else {
          result.data.fields[i].is_numeric = looks_numeric(cols.at(i));
        }
      }
      types_resolved = true;
    }

    QStringList s_date = cols.at(0).split("-");
    QStringList s_time = cols.at(1).split(":");
    QStringList s_secs;

    if (s_time.size() == 3) {
      s_secs = s_time.at(2).split(".");
      if (s_secs.size() == 1) {
        s_secs.append("0");
      }
    }

    if (s_date.size() != 3 || s_time.size() != 3 || s_secs.size() != 2) {
      errors++;
      lines++;
      continue;
    }

    QDate d(s_date.at(0).toInt(), s_date.at(1).toInt(), s_date.at(2).toInt());
    QTime t(s_time.at(0).toInt(), s_time.at(1).toInt(),
            s_secs.at(0).toInt(), s_secs.at(1).toInt());
    if (!d.isValid() || !t.isValid()) {
      errors++;
      lines++;
      continue;
    }

    result.data.timestamps.append(QDateTime(d, t).toMSecsSinceEpoch());

    for (int i = 0; i < num_fields; ++i) {
      auto& field = result.data.fields[i];
      if (field.is_numeric) {
        field.numbers.append(cols.at(i).toDouble());
      }
      else {
        field.strings.append(cols.at(i));
      }
    }

    lines++;
    if (lines % 1024 == 0 && file_size > 0 && progress_func) {
      progress_func(static_cast<int>(file.pos() * 100 / file_size));
    }
  }

  if (progress_func) {
    progress_func(100);
  }

  if (result.data.timestamps.isEmpty()) {
    result.error = QString("Log file contains no data");
    return result;
  }

  result.data.filename = QFileInfo(filename).baseName();
  result.data.errors = errors;
  result.data.lines = lines + 1;

  int n = result.data.timestamps.size();

  QDateTime last = QDateTime::fromMSecsSinceEpoch(result.data.timestamps.at(0));
  for (int i = 1; i < n; ++i) {
    QDateTime current = QDateTime::fromMSecsSinceEpoch(result.data.timestamps.at(i));
    if (last.secsTo(current) > 60) {
      result.data.session_breaks.append(i - 1);
    }
    last = current;
  }
  result.data.session_breaks.append(n - 1);

  return result;
}


plotsCollection compute_plots(const LogsData& data,
                              const PlotRequest& request,
                              std::function<bool()> cancel_check_func)
{
  plotsCollection plots;
  plots.min_x = INVALID_MIN;
  plots.max_x = 0;
  plots.too_many_ranges = false;
  plots.common_min_y = 0;
  plots.common_max_y = 0;

  if (data.is_empty() || request.selected_columns.isEmpty()) {
    return plots;
  }

  bool has_selection = !request.selected_rows.isEmpty();
  int row_count = has_selection ? request.selected_rows.count() : data.row_count();

  for (int f = 0; f < request.selected_columns.count(); ++f) {
    if (cancel_check_func && cancel_check_func()) return plots;

    int col = request.selected_columns.at(f);
    if (col < 0 || col >= data.col_count()) continue;

    auto& field = data.fields.at(col);
    if (!field.is_numeric) continue;

    coords_t c;
    c.min_y = INVALID_MIN;
    c.max_y = INVALID_MAX;
    c.yaxis = firstLeft;
    c.name  = request.field_names.value(f, field.name);
    c.x.reserve(row_count);
    c.y.reserve(row_count);

    for (int r = 0; r < row_count; ++r) {
      int row = has_selection ? request.selected_rows.at(r) : r;
      if (row < 0 || row >= data.row_count()) continue;

      double y = field.numbers.at(row);
      double t = data.timestamps.at(row) / 1000.0;

      c.y.push_back(y);
      if (c.min_y > y) c.min_y = y;
      if (c.max_y < y) c.max_y = y;

      c.x.push_back(t);
      if (plots.min_x == INVALID_MIN) plots.min_x = t;
      else if (plots.min_x > t) plots.min_x = t;
      if (plots.max_x < t) plots.max_x = t;
    }

    if (c.x.isEmpty()) continue;

    double range_inc = (c.max_y - c.min_y) / 100;
    if (range_inc == 0) range_inc = 1;
    c.max_y += range_inc;
    c.min_y -= range_inc;

    plots.coords.append(c);
  }

  if ((cancel_check_func && cancel_check_func()) || plots.coords.isEmpty()) return plots;

  double min_val = 0, max_val = 0;
  bool found = false;
  for (int f = 0; f < request.selected_columns.count(); ++f) {
    int col = request.selected_columns.at(f);
    if (col < 0 || col >= data.col_count()) continue;
    auto& field = data.fields.at(col);
    if (!field.is_numeric) continue;
    for (int r = 0; r < row_count; ++r) {
      int row = has_selection ? request.selected_rows.at(r) : r;
      if (row < 0 || row >= data.row_count()) continue;
      double v = field.numbers.at(row);
      if (!found) {
        min_val = v;
        max_val = v;
        found = true;
      }
      else {
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
      }
    }
  }
  plots.common_min_y = min_val;
  plots.common_max_y = max_val;

  plots.y_axes_ranges[firstLeft].min = plots.coords.at(0).min_y;
  plots.y_axes_ranges[firstLeft].max = plots.coords.at(0).max_y;

  for (int i = firstRight; i < AXES_LIMIT; ++i) {
    plots.y_axes_ranges[i].min = INVALID_MIN;
    plots.y_axes_ranges[i].max = INVALID_MAX;
  }

  for (int i = 0; i < AXES_LIMIT; ++i) {
    plots.y_axes_ratios[i] = 0;
  }

  for (int i = 1; i < plots.coords.size(); ++i) {
    if (cancel_check_func && cancel_check_func()) return plots;

    double actual_range = plots.y_axes_ranges[firstLeft].max
                        - plots.y_axes_ranges[firstLeft].min;
    double this_range = plots.coords.at(i).max_y - plots.coords.at(i).min_y;

    while (plots.y_axes_ranges[plots.coords.at(i).yaxis].max != INVALID_MAX &&
           (this_range > actual_range * 1.3 || this_range * 1.3 < actual_range ||
            plots.coords.at(i).min_y > plots.y_axes_ranges[plots.coords.at(i).yaxis].max ||
            plots.coords.at(i).max_y < plots.y_axes_ranges[plots.coords.at(i).yaxis].min)) {

      if (!request.use_common_axes) {
        switch (plots.coords[i].yaxis) {
          case firstLeft:
            plots.coords[i].yaxis = firstRight;
            break;
          case firstRight:
            plots.coords[i].yaxis = secondLeft;
            break;
          case secondLeft:
            plots.coords[i].yaxis = secondRight;
            break;
          case secondRight:
            plots.too_many_ranges = true;
            break;
        }
        if (plots.too_many_ranges) break;
      }
      actual_range = plots.y_axes_ranges[plots.coords.at(i).yaxis].max
                   - plots.y_axes_ranges[plots.coords.at(i).yaxis].min;
    }

    if (plots.too_many_ranges) break;

    if (plots.coords.at(i).min_y < plots.y_axes_ranges[plots.coords.at(i).yaxis].min)
      plots.y_axes_ranges[plots.coords.at(i).yaxis].min = plots.coords.at(i).min_y;
    if (plots.coords.at(i).max_y > plots.y_axes_ranges[plots.coords.at(i).yaxis].max)
      plots.y_axes_ranges[plots.coords.at(i).yaxis].max = plots.coords.at(i).max_y;
  }

  if (plots.too_many_ranges) {
    plots.y_axes_ranges[firstLeft].max = 101;
    plots.y_axes_ranges[firstLeft].min = -1;

    for (int i = firstRight; i < AXES_LIMIT; ++i) {
      plots.y_axes_ranges[i].min = INVALID_MIN;
      plots.y_axes_ranges[i].max = INVALID_MAX;
    }

    for (int i = 0; i < plots.coords.size(); ++i) {
      plots.coords[i].yaxis = firstLeft;
      double factor = 100.0 / (plots.coords.at(i).max_y - plots.coords.at(i).min_y);

      for (int j = 0; j < plots.coords.at(i).y.count(); ++j) {
        plots.coords[i].y[j] = factor * (plots.coords.at(i).y.at(j) - plots.coords.at(i).min_y);
      }
    }
  }
  else {
    for (int i = firstRight; i < AXES_LIMIT; ++i) {
      if (plots.y_axes_ranges[i].max == INVALID_MAX) break;
      plots.y_axes_ratios[i] =
          (plots.y_axes_ranges[i].max - plots.y_axes_ranges[i].min) /
          (plots.y_axes_ranges[firstLeft].max - plots.y_axes_ranges[firstLeft].min);
    }
  }

  return plots;
}

} // namespace LogsParser