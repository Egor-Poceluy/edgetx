/*
 * Copyright (C) EdgeTX
 * License GPLv2
 */

#pragma once

#include <QtCore>
#include <QDialog>
#include "qcustomplot.h"
#include "logsdata.h"

namespace Ui { class LogsDialog; }

class LogsWorker;
class LogTableModel;
class QThread;

class LogsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LogsDialog(QWidget *parent = 0);
  ~LogsDialog();

signals:
  void requestParse(const QString &fileName);
  void requestPlots(const PlotRequest &req);

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
  void cancelLoad();
  void saveSession();
  void sessionsCurrentIndexChanged(int index);
  void mapsButtonClicked();
  void yAxisChangeRanges(QCPRange range);
  void onParsed(const LogData &data);
  void onParseError(const QString &message);
  void onPlotsReady(const plotsCollection &plots);
  void onPlotsError(const QString &message);
  void onProgress(int percent);

private:
  Ui::LogsDialog *ui;
  QCPAxisRect *axisRect;
  QCPLegend *rightLegend;
  bool plotLock = false;

  LogData _data;
  LogTableModel* _model = nullptr;

  QVarLengthArray<Qt::GlobalColor> colors;
  QPen pen;

  double yAxesRatios[AXES_LIMIT] = {0};
  minMax_t yAxesRanges[AXES_LIMIT];

  QCPItemTracer * tracerMaxAlt = nullptr;
  QCPItemTracer * cursorA = nullptr;
  QCPItemTracer * cursorB = nullptr;
  QCPItemStraightLine * cursorLine = nullptr;

  LogsWorker * worker = nullptr;
  QThread * thread = nullptr;

  QList<QStringList> filterGePoints(const LogData &input);
  void exportToGoogleEarth();
  QDateTime getRecordTimeStamp(int index) const;
  QString generateDuration(const QDateTime & start, const QDateTime & end) const;
  void buildSessionsCombo();

  void addMaxAltitudeMarker(const coords_t & c, QCPGraph * graph);
  void countNumberOfThrows(const coords_t & c, QCPGraph * graph);
  void addCursor(QCPItemTracer ** cursor, QCPGraph * graph, const QColor & color);
  void addCursorLine(QCPItemStraightLine ** line, QCPGraph * graph, const QColor & color);
  void placeCursor(double x, bool second);
  QString formatTimeDelta(double timeDelta);
  void updateCursorsLabel();
};
