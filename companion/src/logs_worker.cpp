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

#include "logs_worker.h"
#include "logs_parser.h"

LogsWorker::LogsWorker(QObject *parent)
  : QObject(parent), _cancelled(false) {}


void LogsWorker::cancel()
{
  _cancelled.store(true, std::memory_order_release);
}


bool LogsWorker::is_cancelled() const
{
  return _cancelled.load(std::memory_order_acquire);
}


void LogsWorker::parse_file(const QString& filename)
{
  _cancelled.store(false, std::memory_order_release);

  auto cancelled_func = [this]() { return is_cancelled(); };
  auto progress_func  = [this](int p) { emit progress(p); };

  auto result = LogsParser::parse_file(filename, progress_func, cancelled_func);

  if (is_cancelled()) {
    emit parse_error(QString("cancelled"));
    return;
  }
  if (!result.error.isEmpty()) {
    emit parse_error(result.error);
    return;
  }

  _data = result.data;
  emit parse_ready(result.data);
}


void LogsWorker::compute_plots(const PlotRequest& request)
{
  _cancelled.store(false, std::memory_order_release);

  if (_data.is_empty()) {
    emit plots_error(tr("No data loaded"));
    return;
  }

  auto cancelled_func = [this]() { return is_cancelled(); };
  auto plots = LogsParser::compute_plots(_data, request, cancelled_func);

  if (is_cancelled()) return;
  emit plots_ready(plots);
}