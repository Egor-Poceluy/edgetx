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
#include <QAtomicInt>
#include "logsdata.h"

class LogsWorker : public QObject
{
  Q_OBJECT

public:
  explicit LogsWorker(QObject* parent = 0);

public slots:
  void parseFile(const QString& fileName);
  void computePlots(const PlotRequest& request);
  void cancel();

signals:
  void parsed(const LogData& data);
  void parseError(const QString& message);
  void plotsReady(const plotsCollection& plots);
  void plotsError(const QString& message);
  void progress(int percent);

private:
  LogData _data;
  QAtomicInt _generation;

  bool isCancelled(int generation) const;
};
