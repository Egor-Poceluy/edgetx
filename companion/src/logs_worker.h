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

#include <QObject>
#include <atomic>
#include "logs_data.h"

class LogsWorker : public QObject
{
  Q_OBJECT

public:
  explicit LogsWorker(QObject* parent = nullptr);

public slots:
  void parse_file(const QString& filename);
  void compute_plots(const PlotRequest& request);
  void cancel();

signals:
  void parse_ready(const LogsData& data);
  void parse_error(const QString& message);
  void plots_ready(const plotsCollection& plots);
  void plots_error(const QString& message);
  void progress(int percent);

private:
  bool is_cancelled() const;

  LogsData _data;
  std::atomic<bool> _cancelled;
};