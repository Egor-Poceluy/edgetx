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

#include <QDateTime>
#include "logsworker.h"
#include "logsparser.h"

LogsWorker::LogsWorker(QObject *parent) : QObject(parent)
{
  _generation.storeRelease(0);
}

void LogsWorker::cancel()
{
  _generation.fetchAndAddRelease(1);
}

bool LogsWorker::isCancelled(int generation) const
{
  return _generation.loadAcquire() != generation;
}

void LogsWorker::parseFile(const QString &fileName)
{
  const int myGen = _generation.loadAcquire();

  auto cancelled = [this, myGen]() { return isCancelled(myGen); };
  auto report = [this](int p) { emit progress(p); };

  LogParser::Result r = LogParser::parseFile(fileName, report, cancelled);

  if (isCancelled(myGen)) {
    emit parseError(QString("cancelled"));
    return;
  }
  if (!r.ok) { emit parseError(r.error); return; }

  _data = r.data;
  emit parsed(r.data);
}

void LogsWorker::computePlots(const PlotRequest &request)
{
  const int myGen = _generation.loadAcquire();

  if (_data.isEmpty()) {
    emit plotsError(tr("No data loaded"));
    return;
  }
  if (request.selectedColumns.isEmpty()) {
    emit plotsReady(plotsCollection{});
    return;
  }

  const bool hasSelection = !request.selectedRows.isEmpty();
  const int rowCount = hasSelection ? request.selectedRows.count() : _data.rowCount();

  plotsCollection plots;
  plots.min_x = INVALID_MIN;
  plots.max_x = 0;
  plots.tooManyRanges = false;
  plots.commonMinY = 0;
  plots.commonMaxY = 0;

  for (int f = 0; f < request.selectedColumns.count(); ++f) {
    if (isCancelled(myGen)) return;

    const int col = request.selectedColumns.at(f);
    if (col < 0 || col >= _data.colCount()) continue;

    const LogField& field = _data.fields.at(col);
    if (!field.isNumeric) continue;

    coords_t c;
    c.min_y = INVALID_MIN;
    c.max_y = INVALID_MAX;
    c.yaxis = firstLeft;
    c.name  = request.fieldNames.value(f, field.name);
    c.x.reserve(rowCount);
    c.y.reserve(rowCount);

    for (int r = 0; r < rowCount; ++r) {
      const int row = hasSelection ? request.selectedRows.at(r) : r;
      if (row < 0 || row >= _data.rowCount()) continue;

      const double y = field.num.at(row);
      const double t = _data.timestamps.at(row) / 1000.0;

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

  if (isCancelled(myGen)) return;

  if (plots.coords.isEmpty()) {
    emit plotsReady(plots);
    return;
  }

  double minVal = 0, maxVal = 0;
  bool found = false;
  for (int f = 0; f < request.selectedColumns.count(); ++f) {
    const int col = request.selectedColumns.at(f);
    if (col < 0 || col >= _data.colCount()) continue;
    const LogField& field = _data.fields.at(col);
    if (!field.isNumeric) continue;
    for (int r = 0; r < rowCount; ++r) {
      const int row = hasSelection ? request.selectedRows.at(r) : r;
      if (row < 0 || row >= _data.rowCount()) continue;
      const double v = field.num.at(row);
      
      if (!found) {
        minVal = v;
        maxVal = v;
        found = true;
      }
      else {
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
      }
    }
  }

  plots.commonMinY = minVal;
  plots.commonMaxY = maxVal;

  plots.yAxesRanges[firstLeft].min = plots.coords.at(0).min_y;
  plots.yAxesRanges[firstLeft].max = plots.coords.at(0).max_y;

  for (int i = firstRight; i < AXES_LIMIT; ++i) {
    plots.yAxesRanges[i].min = INVALID_MIN;
    plots.yAxesRanges[i].max = INVALID_MAX;
  }

  for (int i = 0; i < AXES_LIMIT; ++i) {
    plots.yAxesRatios[i] = 0;
  }

  for (int i = 1; i < plots.coords.size(); ++i) {
    if (isCancelled(myGen)) return;

    double actualRange = plots.yAxesRanges[firstLeft].max
                       - plots.yAxesRanges[firstLeft].min;
    double thisRange = plots.coords.at(i).max_y - plots.coords.at(i).min_y;

    while (plots.yAxesRanges[plots.coords.at(i).yaxis].max != INVALID_MAX &&
           (thisRange > actualRange * 1.3 || thisRange * 1.3 < actualRange ||
            plots.coords.at(i).min_y > plots.yAxesRanges[plots.coords.at(i).yaxis].max ||
            plots.coords.at(i).max_y < plots.yAxesRanges[plots.coords.at(i).yaxis].min)) {

      if (!request.useCommonAxes) {
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
            plots.tooManyRanges = true;
            break;
          default: break;
        }
        if (plots.tooManyRanges) break;
      }
      actualRange = plots.yAxesRanges[plots.coords.at(i).yaxis].max
                  - plots.yAxesRanges[plots.coords.at(i).yaxis].min;
    }

    if (plots.tooManyRanges) break;

    if (plots.coords.at(i).min_y < plots.yAxesRanges[plots.coords.at(i).yaxis].min)
      plots.yAxesRanges[plots.coords.at(i).yaxis].min = plots.coords.at(i).min_y;
    if (plots.coords.at(i).max_y > plots.yAxesRanges[plots.coords.at(i).yaxis].max)
      plots.yAxesRanges[plots.coords.at(i).yaxis].max = plots.coords.at(i).max_y;
  }

  if (plots.tooManyRanges) {
    plots.yAxesRanges[firstLeft].max = 101;
    plots.yAxesRanges[firstLeft].min = -1;

    for (int i = firstRight; i < AXES_LIMIT; ++i) {
      plots.yAxesRanges[i].min = INVALID_MIN;
      plots.yAxesRanges[i].max = INVALID_MAX;
    }

    for (int i = 0; i < plots.coords.size(); ++i) {
      plots.coords[i].yaxis = firstLeft;
      const double factor = 100.0 / (plots.coords.at(i).max_y - plots.coords.at(i).min_y);

      for (int j = 0; j < plots.coords.at(i).y.count(); ++j) {
        plots.coords[i].y[j] = factor * (plots.coords.at(i).y.at(j) - plots.coords.at(i).min_y);
      }
    }
  }
  else {
    for (int i = firstRight; i < AXES_LIMIT; ++i) {
      if (plots.yAxesRanges[i].max == INVALID_MAX) break;
      plots.yAxesRatios[i] =
          (plots.yAxesRanges[i].max - plots.yAxesRanges[i].min) /
          (plots.yAxesRanges[firstLeft].max - plots.yAxesRanges[firstLeft].min);
    }
  }

  if (isCancelled(myGen)) return;
  emit plotsReady(plots);
}
