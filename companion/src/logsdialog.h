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

#include <QtCore>
#include <QDialog>
#include "qcustomplot.h"
#include "logs_data.h"

namespace Ui { class LogsDialog; }

class LogsWorker;
class LogsTableModel;
class QThread;

class LogsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LogsDialog(QWidget *parent = 0);
  ~LogsDialog();

signals:
  void request_parse(const QString& filename);
  void request_plots(const PlotRequest& request);

private slots:
  void titleDoubleClicked(QMouseEvent *evt);
  void axisLabelDoubleClick(QCPAxis* axis, QCPAxis::SelectablePart part);
  void legendDoubleClick(QCPLegend* legend, QCPAbstractLegendItem* item);
  void selectionChanged();
  void mousePress(QMouseEvent * event);
  void mouseWheel();
  void removeAllGraphs();
  void plotLogs();
  void fileOpen();
  void saveSession();
  void sessionsCurrentIndexChanged(int index);
  void mapsButtonClicked();
  void yAxisChangeRanges(QCPRange range);
  void cancel_load();
  void on_parsed(const LogsData& data);
  void on_parse_error(const QString& message);
  void on_plots_ready(const plotsCollection& plots);
  void on_plots_error(const QString& message);
  void on_progress(int percent);

private:
  Ui::LogsDialog *ui;
  QCPAxisRect *axisRect;
  QCPLegend *rightLegend;
  bool plotLock = false;

  LogsData _data;
  LogsTableModel* _model = nullptr;

  QVarLengthArray<Qt::GlobalColor> colors;
  QPen pen;

  double yAxesRatios[AXES_LIMIT] = {0};
  minMax_t yAxesRanges[AXES_LIMIT];

  QCPItemTracer * tracerMaxAlt = nullptr;
  QCPItemTracer * cursorA = nullptr;
  QCPItemTracer * cursorB = nullptr;
  QCPItemStraightLine * cursorLine = nullptr;

  LogsWorker* worker = nullptr;
  QThread* thread = nullptr;

  QList<QStringList> filterGePoints(const LogsData& input);
  void exportToGoogleEarth();
  QDateTime getRecordTimeStamp(int index) const;
  QString generateDuration(const QDateTime & start, const QDateTime & end) const;
  void build_sessions_combo();

  void addMaxAltitudeMarker(const coords_t & c, QCPGraph * graph);
  void countNumberOfThrows(const coords_t & c, QCPGraph * graph);
  void addCursor(QCPItemTracer ** cursor, QCPGraph * graph, const QColor & color);
  void addCursorLine(QCPItemStraightLine ** line, QCPGraph * graph, const QColor & color);
  void placeCursor(double x, bool second);
  QString formatTimeDelta(double timeDelta);
  void updateCursorsLabel();
};
