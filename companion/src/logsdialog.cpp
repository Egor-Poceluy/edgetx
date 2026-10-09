/*
 * Copyright (C) EdgeTX
 * License GPLv2
 */

#include <QThread>
#include <math.h>
#include "logsdialog.h"
#include "logsworker.h"
#include "logstablemodel.h"
#include "appdata.h"
#include "ui_logsdialog.h"
#include "helpers.h"
#if defined _MSC_VER || !defined __GNUC__
#include <windows.h>
#else
#include <unistd.h>
#endif

LogsDialog::LogsDialog(QWidget *parent) :
  QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
  ui(new Ui::LogsDialog)
{
  qRegisterMetaType<LogData>("LogData");
  qRegisterMetaType<PlotRequest>("PlotRequest");
  qRegisterMetaType<plotsCollection>("plotsCollection");

  ui->setupUi(this);
  setWindowIcon(CompanionIcon("logs.png"));

  colors << Qt::green << Qt::red << Qt::magenta << Qt::cyan
         << Qt::blue  << Qt::yellow << Qt::darkGreen << Qt::darkRed
         << Qt::darkMagenta << Qt::darkCyan << Qt::darkBlue << Qt::darkYellow;
  pen.setWidthF(1.0);

  QCPTextElement *title = new QCPTextElement(ui->customPlot);
  QFont titlefont = font();
  titlefont.setBold(true);
  title->setText(tr("Telemetry logs"));
  title->setFont(titlefont);

  ui->customPlot->plotLayout()->insertRow(0);
  ui->customPlot->plotLayout()->addElement(0, 0, title);
  ui->customPlot->setNoAntialiasingOnDrag(true);
  ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom |
                                  QCP::iSelectAxes | QCP::iSelectLegend |
                                  QCP::iSelectPlottables);

  axisRect = ui->customPlot->axisRect();

  axisRect->axis(QCPAxis::atBottom)->setLabel(tr("Time (hh:mm:ss.ms)"));
  QSharedPointer<QCPAxisTickerDateTime> timeTicker(new QCPAxisTickerDateTime);
  timeTicker->setDateTimeFormat("hh:mm:ss.zzz");
  axisRect->axis(QCPAxis::atBottom)->setTicker(timeTicker);
  QDateTime now = QDateTime::currentDateTime();
  axisRect->axis(QCPAxis::atBottom)->setRange(
      QCPAxisTickerDateTime::dateTimeToKey(now.addSecs(-60 * 60 * 2)),
      QCPAxisTickerDateTime::dateTimeToKey(now));
  axisRect->axis(QCPAxis::atLeft)->setTickLabels(false);
  axisRect->addAxis(QCPAxis::atLeft);
  axisRect->addAxis(QCPAxis::atRight);
  axisRect->axis(QCPAxis::atLeft, 1)->setVisible(false);
  axisRect->axis(QCPAxis::atRight, 1)->setVisible(false);

  QFont legendFont = font();
  legendFont.setPointSize(10);
  ui->customPlot->legend->setFont(legendFont);
  ui->customPlot->legend->setSelectedFont(legendFont);
  axisRect->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignLeft);

  rightLegend = new QCPLegend;
  axisRect->insetLayout()->addElement(rightLegend, Qt::AlignTop | Qt::AlignRight);
  rightLegend->setLayer("legend");
  rightLegend->setFont(legendFont);
  rightLegend->setSelectedFont(legendFont);
  rightLegend->setVisible(false);

  ui->customPlot->setAutoAddPlottableToLegend(false);

  QString path = g.gePath();
  if (path.isEmpty() || !QFile(path).exists())
    ui->mapsButton->hide();

  ui->SaveSession_PB->setEnabled(false);

  _model = new LogTableModel(this);
  ui->logTable->setModel(_model);
  ui->logTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  ui->progressBar->setVisible(false);
  ui->cancelLoad_PB->setVisible(false);

  connect(ui->customPlot, &QCustomPlot::selectionChangedByUser, this, &LogsDialog::selectionChanged);
  connect(ui->customPlot, &QCustomPlot::mousePress, this, &LogsDialog::mousePress);
  connect(ui->customPlot, &QCustomPlot::mouseWheel, this, &LogsDialog::mouseWheel);
  connect(axisRect->axis(QCPAxis::atLeft),
          static_cast<void(QCPAxis::*)(const QCPRange&)>(&QCPAxis::rangeChanged),
          this, &LogsDialog::yAxisChangeRanges);
  connect(title, &QCPTextElement::doubleClicked, this, &LogsDialog::titleDoubleClicked);
  connect(ui->customPlot, &QCustomPlot::axisDoubleClick, this, &LogsDialog::axisLabelDoubleClick);
  connect(ui->customPlot, &QCustomPlot::legendDoubleClick, this, &LogsDialog::legendDoubleClick);
  connect(ui->FieldsTW, &QTableWidget::itemSelectionChanged, this, &LogsDialog::plotLogs);
  connect(ui->logTable->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &LogsDialog::plotLogs);
  connect(ui->Reset_PB, &QPushButton::clicked, this, [this]() {
    ui->ZoomX_ChkB->setChecked(false);
    ui->ZoomY_ChkB->setChecked(false);
    ui->CommonAxes_ChkB->setChecked(false);
    plotLogs();
  });
  connect(ui->CommonAxes_ChkB, &QPushButton::clicked, this, &LogsDialog::plotLogs);
  connect(ui->SaveSession_PB, &QPushButton::clicked, this, &LogsDialog::saveSession);
  connect(ui->fileOpen_PB, &QPushButton::clicked, this, &LogsDialog::fileOpen);
  connect(ui->cancelLoad_PB, &QPushButton::clicked, this, &LogsDialog::cancelLoad);
  connect(ui->mapsButton, &QPushButton::clicked, this, &LogsDialog::mapsButtonClicked);
  connect(ui->sessions_CB,
          static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
          this, &LogsDialog::sessionsCurrentIndexChanged);

  thread = new QThread(this);
  worker = new LogsWorker();
  worker->moveToThread(thread);

  connect(this, &LogsDialog::requestParse, worker, &LogsWorker::parseFile);
  connect(this, &LogsDialog::requestPlots, worker, &LogsWorker::computePlots);

  connect(worker, &LogsWorker::parsed,     this, &LogsDialog::onParsed);
  connect(worker, &LogsWorker::parseError, this, &LogsDialog::onParseError);
  connect(worker, &LogsWorker::plotsReady, this, &LogsDialog::onPlotsReady);
  connect(worker, &LogsWorker::plotsError, this, &LogsDialog::onPlotsError);
  connect(worker, &LogsWorker::progress,   this, &LogsDialog::onProgress);

  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  thread->start();
}

LogsDialog::~LogsDialog()
{
  if (thread) {
    if (worker) worker->cancel();
    thread->quit();
    thread->wait();
  }
  delete ui;
}

void LogsDialog::fileOpen()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select your log file"),
                                                  g.logDir());
  if (fileName.isEmpty()) return;

  g.logDir(fileName);
  ui->FileName_LE->setText(fileName);

  if (worker) worker->cancel();

  ui->progressBar->setValue(0);
  ui->progressBar->setVisible(true);
  ui->cancelLoad_PB->setVisible(true);
  ui->fileOpen_PB->setEnabled(false);

  emit requestParse(fileName);
}

void LogsDialog::cancelLoad()
{
  if (worker) worker->cancel();
}

void LogsDialog::onProgress(int percent)
{
  ui->progressBar->setValue(percent);
}

void LogsDialog::onParseError(const QString &message)
{
  ui->progressBar->setVisible(false);
  ui->cancelLoad_PB->setVisible(false);
  ui->fileOpen_PB->setEnabled(true);
  if (!message.isEmpty() && message != "cancelled")
    QMessageBox::warning(this, CPN_STR_APP_NAME, message);
}

void LogsDialog::onParsed(const LogData &data)
{
  ui->progressBar->setVisible(false);
  ui->cancelLoad_PB->setVisible(false);
  ui->fileOpen_PB->setEnabled(true);

  if (data.errors > 1) {
    QMessageBox::warning(this, CPN_STR_APP_NAME,
      tr("The selected logfile contains %1 invalid lines out of  %2 total lines")
          .arg(data.errors).arg(data.lines));
  }

  _data = data;
  plotLock = true;

  _model->setLog(data);
  ui->logTable->horizontalHeader()->setResizeContentsPrecision(200);
  ui->logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  ui->logTable->horizontalHeader()->setStretchLastSection(false);

  ui->FieldsTW->clear();
  ui->FieldsTW->setShowGrid(false);
  ui->FieldsTW->setContentsMargins(0, 0, 0, 0);
  ui->FieldsTW->setRowCount(qMax(0, data.colCount() - 2));
  ui->FieldsTW->setColumnCount(1);
  ui->FieldsTW->setHorizontalHeaderLabels(QStringList(tr("Available fields")));
  for (int i = 2; i < data.colCount(); ++i) {
    QTableWidgetItem *item = new QTableWidgetItem(data.fields.at(i).name);
    ui->FieldsTW->setItem(i - 2, 0, item);
  }
  ui->FieldsTW->resizeRowsToContents();

  buildSessionsCombo();

  plotLock = false;
}

void LogsDialog::plotLogs()
{
  if (plotLock) return;

  if (!ui->FieldsTW->selectedItems().length()) {
    removeAllGraphs();
    return;
  }

  PlotRequest req;
  req.useCommonAxes = ui->CommonAxes_ChkB->isChecked();

  for (QTableWidgetItem *plot : ui->FieldsTW->selectedItems()) {
    req.selectedColumns.append(plot->row() + 2);
    req.fieldNames.append(plot->text());
  }

  for (const QModelIndex &idx : ui->logTable->selectionModel()->selectedRows())
    req.selectedRows.append(idx.row());

  emit requestPlots(req);
}

void LogsDialog::onPlotsError(const QString &message)
{
  QMessageBox::warning(this, CPN_STR_APP_NAME, message);
}

void LogsDialog::onPlotsReady(const plotsCollection &plots)
{
  for (int i = 0; i < AXES_LIMIT; ++i) {
    yAxesRanges[i] = plots.yAxesRanges[i];
    yAxesRatios[i] = plots.yAxesRatios[i];
  }

  removeAllGraphs();

  if (plots.coords.isEmpty()) {
    ui->customPlot->replot();
    return;
  }

  axisRect->axis(QCPAxis::atBottom)->setRange(plots.min_x, plots.max_x);

  if (ui->CommonAxes_ChkB->isChecked())
    axisRect->axis(QCPAxis::atLeft)->setRange(plots.commonMinY, plots.commonMaxY);
  else
    axisRect->axis(QCPAxis::atLeft)->setRange(yAxesRanges[firstLeft].min,
                                              yAxesRanges[firstLeft].max);

  axisRect->axis(QCPAxis::atLeft)->setTickLabels(!plots.tooManyRanges);

  if (yAxesRanges[firstRight].max != INVALID_MAX) {
    axisRect->axis(QCPAxis::atRight)->setRange(yAxesRanges[firstRight].min,
                                               yAxesRanges[firstRight].max);
    axisRect->axis(QCPAxis::atRight)->setVisible(true);
    rightLegend->setVisible(true);

    if (yAxesRanges[secondLeft].max != INVALID_MAX) {
      axisRect->axis(QCPAxis::atLeft, 1)->setVisible(true);
      axisRect->axis(QCPAxis::atLeft, 1)->setRange(yAxesRanges[secondLeft].min,
                                                   yAxesRanges[secondLeft].max);

      if (yAxesRanges[secondRight].max != INVALID_MAX) {
        axisRect->axis(QCPAxis::atRight, 1)->setVisible(true);
        axisRect->axis(QCPAxis::atRight, 1)->setRange(yAxesRanges[secondRight].min,
                                                      yAxesRanges[secondRight].max);
      }
    }
  }

  for (int i = 0; i < plots.coords.size(); ++i) {
    switch (plots.coords[i].yaxis) {
      case firstLeft:
        ui->customPlot->addGraph();
        if (yAxesRanges[secondLeft].max != INVALID_MAX)
          ui->customPlot->graph(i)->setName(plots.coords.at(i).name + tr(" (L1)"));
        else
          ui->customPlot->graph(i)->setName(plots.coords.at(i).name);
        ui->customPlot->legend->addItem(
            new QCPPlottableLegendItem(ui->customPlot->legend, ui->customPlot->graph(i)));
        break;
      case firstRight:
        ui->customPlot->addGraph(axisRect->axis(QCPAxis::atBottom),
                                 axisRect->axis(QCPAxis::atRight));
        if (yAxesRanges[secondRight].max != INVALID_MAX)
          ui->customPlot->graph(i)->setName(plots.coords.at(i).name + tr(" (R1)"));
        else
          ui->customPlot->graph(i)->setName(plots.coords.at(i).name);
        rightLegend->addItem(
            new QCPPlottableLegendItem(rightLegend, ui->customPlot->graph(i)));
        break;
      case secondLeft:
        ui->customPlot->addGraph(axisRect->axis(QCPAxis::atBottom),
                                 axisRect->axis(QCPAxis::atLeft, 1));
        ui->customPlot->graph(i)->setName(plots.coords.at(i).name + tr(" (L2)"));
        ui->customPlot->legend->addItem(
            new QCPPlottableLegendItem(ui->customPlot->legend, ui->customPlot->graph(i)));
        break;
      case secondRight:
        ui->customPlot->addGraph(axisRect->axis(QCPAxis::atBottom),
                                 axisRect->axis(QCPAxis::atRight, 1));
        ui->customPlot->graph(i)->setName(plots.coords.at(i).name + tr(" (R2)"));
        rightLegend->addItem(
            new QCPPlottableLegendItem(rightLegend, ui->customPlot->graph(i)));
        break;
      default: break;
    }

    ui->customPlot->graph(i)->setData(plots.coords.at(i).x, plots.coords.at(i).y);
    pen.setColor(colors.at(i % colors.size()));
    ui->customPlot->graph(i)->setPen(pen);

    if (!tracerMaxAlt && (plots.coords.at(i).name.endsWith("(m)") ||
        plots.coords.at(i).name.endsWith(" Alt") ||
        plots.coords.at(i).name.endsWith("(ft)"))) {
      addMaxAltitudeMarker(plots.coords.at(i), ui->customPlot->graph(i));
      countNumberOfThrows(plots.coords.at(i), ui->customPlot->graph(i));
      addCursor(&cursorA, ui->customPlot->graph(i), Qt::blue);
      addCursor(&cursorB, ui->customPlot->graph(i), Qt::red);
      addCursorLine(&cursorLine, ui->customPlot->graph(i), Qt::black);
      updateCursorsLabel();
    }
  }

  ui->customPlot->legend->setVisible(true);
  ui->customPlot->replot();
}

QDateTime LogsDialog::getRecordTimeStamp(int index) const
{
  if (index < 0 || index >= _data.timestamps.size()) return {};
  return QDateTime::fromMSecsSinceEpoch(_data.timestamps.at(index));
}

QString LogsDialog::generateDuration(const QDateTime &start,
                                     const QDateTime &end) const
{
  int secs = start.secsTo(end);
  QString s;
  if (secs >= 3600) { s = QString("%1:").arg(secs / 3600); secs %= 3600; }
  s += QString("%1:%2").arg(secs / 60, 2, 10, QChar('0'))
                       .arg(secs % 60, 2, 10, QChar('0'));
  return s;
}

void LogsDialog::buildSessionsCombo()
{
  ui->sessions_CB->blockSignals(true);
  ui->sessions_CB->clear();
  ui->SaveSession_PB->setEnabled(false);

  if (_data.isEmpty()) { ui->sessions_CB->blockSignals(false); return; }

  const QList<int> &breaks = _data.sessionBreaks;
  const int n = _data.rowCount();
  const int numSessions = breaks.size();

  QString label = QString("%1 ").arg(numSessions);
  label += tr(numSessions > 1 ? "sessions" : "session");
  label += " <" + tr("time span ") +
           generateDuration(getRecordTimeStamp(0), getRecordTimeStamp(n - 1)) + ">";
  ui->sessions_CB->addItem(label);

  if (numSessions > 1) {
    for (int k = 1; k <= numSessions; ++k) {
      const int start = (k == 1) ? 0 : breaks.at(k - 2) + 1;
      const int end   = breaks.at(k - 1);
      QDateTime s = getRecordTimeStamp(start);
      QDateTime e = getRecordTimeStamp(end);
      QString lbl = s.toString("HH:mm:ss") + " <" + tr("duration ") + generateDuration(s, e) + ">";
      ui->sessions_CB->addItem(lbl, start);
    }
  }
  ui->sessions_CB->blockSignals(false);
}

void LogsDialog::sessionsCurrentIndexChanged(int index)
{
  if (plotLock) return;
  plotLock = true;

  ui->logTable->clearSelection();

  if (index > 0) {
    const int start = ui->sessions_CB->itemData(index, Qt::UserRole).toInt();
    const int end   = _data.sessionBreaks.at(index - 1);

    if (start >= 0 && end >= start && end < _data.rowCount()) {
      QItemSelection sel(_model->index(start, 0),
                         _model->index(end, _data.colCount() - 1));
      ui->logTable->selectionModel()->select(sel, QItemSelectionModel::Select);
    }
    ui->SaveSession_PB->setEnabled(true);
  } else {
    ui->SaveSession_PB->setEnabled(false);
  }

  plotLock = false;
  plotLogs();
}

void LogsDialog::saveSession()
{
  int index = ui->sessions_CB->currentIndex();
  if (index <= 0) return;

  const int n = _data.rowCount();
  QList<QStringList> sessionRows;
  sessionRows.push_back(_data.columnNames);

  int currentSession = 0;
  QDateTime last;
  for (int i = 1; i < n; ++i) {
    QDateTime cur = getRecordTimeStamp(i);
    if (last.isValid() && last.secsTo(cur) > 60) currentSession++;
    last = cur;

    if (currentSession == index) {
      QStringList row;
      row.reserve(_data.colCount());
      for (int c = 0; c < _data.colCount(); ++c) {
        const LogField& f = _data.fields.at(c);
        if (f.isNumeric) row << QString::number(f.num.at(i));
        else             row << f.str.at(i);
      }
      sessionRows.push_back(row);
    } else if (currentSession > index) {
      break;
    }
  }

  QString newFilename = _data.logFilename;
  newFilename.append(QString("-Session%1.csv").arg(index));
  QString filename = QFileDialog::getSaveFileName(this, "Save log", newFilename,
                                                  "CSV files (.csv);");
  if (filename.isEmpty()) return;

  QFile data(filename);
  if (data.open(QFile::WriteOnly | QFile::Truncate)) {
    QTextStream out(&data);
    for (const QStringList& row : sessionRows)
      out << row.join(",") << '\n';
  }
}

QList<QStringList> LogsDialog::filterGePoints(const LogData &input)
{
  QList<QStringList> result;
  if (input.isEmpty()) return result;

  int gpscol = -1;
  for (int i = 0; i < input.colCount(); ++i) {
    if (input.fields.at(i).name == "GPS") gpscol = i;
  }
  if (gpscol < 0) {
    QMessageBox::critical(this, tr("Error: no GPS data found"),
      tr("The column containing GPS coordinates must be named \"GPS\".\n\n\
The columns for altitude \"GAlt\" and for speed \"GSpd\" are optional"));
    return result;
  }

  QStringList header;
  header << "Date" << "Time";
  for (int i = 2; i < input.colCount(); ++i) header << input.fields.at(i).name;
  result.append(header);

  QModelIndexList selectedRows = ui->logTable->selectionModel()->selectedRows();
  QSet<int> selectedSet;
  for (const QModelIndex &idx : selectedRows) selectedSet.insert(idx.row());
  const bool rangeSelected = !selectedSet.isEmpty();

  GpsGlitchFilter glitchFilter;
  GpsLatLonFilter latLonFilter;

  for (int r = 0; r < input.rowCount(); ++r) {
    if (rangeSelected && !selectedSet.contains(r)) continue;

    const QString& gpsStr = input.fields.at(gpscol).str.at(r);
    GpsCoord coord = extractGpsCoordinates(gpsStr);
    if (glitchFilter.isGlitch(coord)) continue;
    if (!latLonFilter.isValid(coord)) continue;

    QStringList row;
    row << input.fields.at(0).str.at(r);
    row << input.fields.at(1).str.at(r);
    for (int c = 2; c < input.colCount(); ++c) {
      const LogField& f = input.fields.at(c);
      if (f.isNumeric) row << QString::number(f.num.at(r));
      else             row << f.str.at(r);
    }
    result.append(row);
  }
  return result;
}

void LogsDialog::exportToGoogleEarth()
{
  QList<QStringList> dataPoints = filterGePoints(_data);
  int n = dataPoints.count();
  if (n <= 1) return;

  int gpscol = 0, altcol = 0, speedcol = 0, datecol = 0, timecol = 0;
  double altMultiplier = 1.0;
  QSet<int> extradataCols;

  for (int i = 1; i < dataPoints.at(0).count(); ++i) {
    bool incExtraData = true;
    if (dataPoints.at(0).at(i) == "Date") { datecol = i; incExtraData = false; }
    else if (dataPoints.at(0).at(i) == "Time") { timecol = i; incExtraData = false; }
    else if (dataPoints.at(0).at(i) == "GPS") { gpscol = i; incExtraData = false; }
    else if (dataPoints.at(0).at(i).contains("GAlt")) {
      altcol = i; incExtraData = false;
      if (dataPoints.at(0).at(i).contains("(ft)")) altMultiplier = 0.3048;
    } else if (dataPoints.at(0).at(i).contains("GSpd")) {
      speedcol = i; incExtraData = false;
    }
    if (incExtraData) {
      for (int j = 0; j < ui->FieldsTW->rowCount(); ++j) {
        auto *it = ui->FieldsTW->item(j, 0);
        if (it && it->isSelected() &&
            it->data(Qt::DisplayRole).toString() == dataPoints.at(0).at(i))
          extradataCols << i;
      }
    }
  }
  if (gpscol == 0) return;

  const QString geFilename = generateProcessUniqueTempFileName("flight.kml");
  QFile geFile(geFilename);
  if (!geFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    QMessageBox::warning(this, CPN_STR_TTL_ERROR,
        tr("Cannot write file %1:\n%2.").arg(geFilename).arg(geFile.errorString()));
    return;
  }

  const QString geIconFilename = QStringLiteral("track0.png");
  const QString geIconPath = QFileInfo(geFile).absolutePath() + "/" + geIconFilename;
  if (!QFile::exists(geIconPath))
    QFile::copy(":/images/" + geIconFilename, geIconPath);

  QTextStream outputStream(&geFile);
  outputStream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n";
  outputStream << "\t<Document>\n\t\t<name>" << _data.logFilename << "</name>\n";
  outputStream << "\t\t<Style id=\"multiTrack_n\">\n\t\t\t<IconStyle>\n\t\t\t\t<Icon>\n\t\t\t\t\t<href>" << geIconFilename << "</href>\n\t\t\t\t</Icon>\n\t\t\t</IconStyle>\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>6</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<Style id=\"multiTrack_h\">\n\t\t\t<IconStyle>\n\t\t\t\t<scale>0</scale>\n\t\t\t\t<Icon>\n\t\t\t\t\t<href>" << geIconFilename << "</href>\n\t\t\t\t</Icon>\n\t\t\t</IconStyle>\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>8</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<StyleMap id=\"multiTrack\">\n\t\t\t<Pair>\n\t\t\t\t<key>normal</key>\n\t\t\t\t<styleUrl>#multiTrack_n</styleUrl>\n\t\t\t</Pair>\n\t\t\t<Pair>\n\t\t\t\t<key>highlight</key>\n\t\t\t\t<styleUrl>#multiTrack_h</styleUrl>\n\t\t\t</Pair>\n\t\t</StyleMap>\n";
  outputStream << "\t\t<Style id=\"lineStyle\">\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>6</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<Schema id=\"schema\">\n";
  outputStream << "\t\t\t<gx:SimpleArrayField name=\"GPSSpeed\" type=\"float\">\n\t\t\t\t<displayName>GPS Speed</displayName>\n\t\t\t</gx:SimpleArrayField>\n";

  for (auto i = extradataCols.cbegin(), end = extradataCols.cend(); i != end; ++i) {
    QString origName = dataPoints.at(0).at(*i);
    QString safeName = origName; safeName.replace(" ", "_");
    outputStream << "\t\t\t<gx:SimpleArrayField name=\"" << safeName
                 << "\" type=\"string\">\n\t\t\t\t<displayName>"
                 << origName << "</displayName>\n\t\t\t</gx:SimpleArrayField>\n";
  }

  QString planeName = _data.logFilename;
  int dash = planeName.indexOf("-");
  if (dash > 0) planeName = planeName.left(dash);

  outputStream << "\t\t</Schema>\n\t\t<Folder>\n\t\t\t<name>Log Data</name>\n\t\t\t<Placemark>\n\t\t\t\t<name>" << planeName << "</name>";
  outputStream << "\n\t\t\t\t<styleUrl>#multiTrack</styleUrl>";
  outputStream << "\n\t\t\t\t<gx:Track>\n\n\t\t\t\t\t<altitudeMode>absolute</altitudeMode>\n";

  for (int i = 1; i < n; ++i)
    outputStream << "\t\t\t\t\t<when>" << dataPoints.at(i).at(datecol) << "T"
                 << dataPoints.at(i).at(timecol) << "Z</when>\n";

  outputStream.setRealNumberNotation(QTextStream::FixedNotation);
  outputStream.setRealNumberPrecision(8);
  for (int i = 1; i < n; ++i) {
    GpsCoord coord = extractGpsCoordinates(dataPoints.at(i).at(gpscol));
    int altitude = altcol ? (dataPoints.at(i).at(altcol).toFloat() * altMultiplier) : 0;
    outputStream << "\t\t\t\t\t<gx:coord>" << coord.longitude << " " << coord.latitude
                 << " " << altitude << " </gx:coord>\n";
  }

  outputStream << "\t\t\t\t\t<ExtendedData>\n\t\t\t\t\t\t<SchemaData schemaUrl=\"#schema\">\n";
  if (speedcol) {
    outputStream << "\t\t\t\t\t\t\t<gx:SimpleArrayData name=\"GPSSpeed\">\n";
    for (int i = 1; i < n; ++i)
      outputStream << "\t\t\t\t\t\t\t\t<gx:value>" << dataPoints.at(i).at(speedcol) << "</gx:value>\n";
    outputStream << "\t\t\t\t\t\t\t</gx:SimpleArrayData>\n";
  }
  for (auto i = extradataCols.cbegin(), end = extradataCols.cend(); i != end; ++i) {
    QString safeName = dataPoints.at(0).at(*i); safeName.replace(" ", "_");
    outputStream << "\t\t\t\t\t\t\t<gx:SimpleArrayData name=\"" << safeName << "\">\n";
    for (int j = 1; j < n; ++j)
      outputStream << "\t\t\t\t\t\t\t\t<gx:value>" << dataPoints.at(j).at(*i) << "</gx:value>\n";
    outputStream << "\t\t\t\t\t\t\t</gx:SimpleArrayData>\n";
  }
  outputStream << "\t\t\t\t\t\t</SchemaData>\n\t\t\t\t\t</ExtendedData>\n\t\t\t\t</gx:Track>\n\t\t\t</Placemark>\n\t\t</Folder>\n\t</Document>\n</kml>";
  geFile.close();

  QString gePath = g.gePath();
  QStringList parameters;
#ifdef __APPLE__
  parameters << "-a" << gePath;
  gePath = "/usr/bin/open";
#endif
  parameters << geFilename;
  QProcess::startDetached(gePath, parameters);
}

void LogsDialog::mapsButtonClicked()
{
  ui->FieldsTW->setDisabled(true);
  ui->logTable->setDisabled(true);
  exportToGoogleEarth();
  ui->FieldsTW->setDisabled(false);
  ui->logTable->setDisabled(false);
}

void LogsDialog::titleDoubleClicked(QMouseEvent *evt)
{
  Q_UNUSED(evt);
  QCPTextElement *title = qobject_cast<QCPTextElement*>(sender());
  if (title) {
    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("Plot Title Change"),
                                             tr("New plot title:"), QLineEdit::Normal,
                                             title->text(), &ok);
    if (ok) { title->setText(newTitle); ui->customPlot->replot(); }
  }
}

void LogsDialog::axisLabelDoubleClick(QCPAxis *axis, QCPAxis::SelectablePart part)
{
  if (part == QCPAxis::spAxisLabel) {
    bool ok;
    QString newLabel = QInputDialog::getText(this, tr("Axis Label Change"),
                                             tr("New axis label:"), QLineEdit::Normal,
                                             axis->label(), &ok);
    if (ok) { axis->setLabel(newLabel); ui->customPlot->replot(); }
  }
}

void LogsDialog::legendDoubleClick(QCPLegend *legend, QCPAbstractLegendItem *item)
{
  Q_UNUSED(legend);
  if (item) {
    QCPPlottableLegendItem *plItem = qobject_cast<QCPPlottableLegendItem*>(item);
    bool ok;
    QString newName = QInputDialog::getText(this, tr("Graph Name Change"),
                                            tr("New graph name:"), QLineEdit::Normal,
                                            plItem->plottable()->name(), &ok);
    if (ok) { plItem->plottable()->setName(newName); ui->customPlot->replot(); }
  }
}

void LogsDialog::selectionChanged()
{
  if (plotLock) return;

  if (axisRect->axis(QCPAxis::atBottom)->selectedParts().testFlag(QCPAxis::spAxis) ||
      axisRect->axis(QCPAxis::atBottom)->selectedParts().testFlag(QCPAxis::spTickLabels))
    axisRect->axis(QCPAxis::atBottom)->setSelectedParts(
        QCPAxis::spAxis | QCPAxis::spTickLabels);

  if (axisRect->axis(QCPAxis::atLeft)->selectedParts().testFlag(QCPAxis::spAxis) ||
      axisRect->axis(QCPAxis::atLeft)->selectedParts().testFlag(QCPAxis::spTickLabels) ||
      (axisRect->axis(QCPAxis::atRight)->visible() &&
       (axisRect->axis(QCPAxis::atRight)->selectedParts().testFlag(QCPAxis::spAxis) ||
        axisRect->axis(QCPAxis::atRight)->selectedParts().testFlag(QCPAxis::spTickLabels))) ||
      (axisRect->axis(QCPAxis::atLeft, 1)->visible() &&
       (axisRect->axis(QCPAxis::atLeft, 1)->selectedParts().testFlag(QCPAxis::spAxis) ||
        axisRect->axis(QCPAxis::atLeft, 1)->selectedParts().testFlag(QCPAxis::spTickLabels))) ||
      (axisRect->axis(QCPAxis::atRight, 1)->visible() &&
       (axisRect->axis(QCPAxis::atRight, 1)->selectedParts().testFlag(QCPAxis::spAxis) ||
        axisRect->axis(QCPAxis::atRight, 1)->selectedParts().testFlag(QCPAxis::spTickLabels)))) {

    axisRect->axis(QCPAxis::atLeft)->setSelectedParts(
        QCPAxis::spAxis | QCPAxis::spTickLabels);
    if (axisRect->axis(QCPAxis::atRight)->visible()) {
      axisRect->axis(QCPAxis::atRight)->setSelectedParts(
          QCPAxis::spAxis | QCPAxis::spTickLabels);
      if (axisRect->axis(QCPAxis::atLeft, 1)->visible()) {
        axisRect->axis(QCPAxis::atLeft, 1)->setSelectedParts(
            QCPAxis::spAxis | QCPAxis::spTickLabels);
        if (axisRect->axis(QCPAxis::atRight, 1)->visible())
          axisRect->axis(QCPAxis::atRight, 1)->setSelectedParts(
              QCPAxis::spAxis | QCPAxis::spTickLabels);
      }
    }
  }

  for (int i = 0; i < ui->customPlot->graphCount(); ++i) {
    QCPGraph *graph = ui->customPlot->graph(i);
    QCPPlottableLegendItem *item = ui->customPlot->legend->itemWithPlottable(graph);
    if (item == NULL) item = rightLegend->itemWithPlottable(graph);
    if (!item) continue;
    if (item->selected() || graph->selected()) {
      item->setSelected(true);
      graph->setSelection(QCPDataSelection(graph->data()->dataRange()));
    }
  }
}

void LogsDialog::mousePress(QMouseEvent * event)
{
  if (axisRect->axis(QCPAxis::atBottom)->selectedParts().testFlag(QCPAxis::spAxis))
    axisRect->setRangeDrag(axisRect->axis(QCPAxis::atBottom)->orientation());
  else if (axisRect->axis(QCPAxis::atLeft)->selectedParts().testFlag(QCPAxis::spAxis))
    axisRect->setRangeDrag(axisRect->axis(QCPAxis::atLeft)->orientation());
  else
    axisRect->setRangeDrag(Qt::Horizontal | Qt::Vertical);

  if (event->button() == Qt::RightButton) {
    double x = axisRect->axis(QCPAxis::atBottom)->pixelToCoord(event->pos().x());
    placeCursor(x, event->modifiers() & Qt::ShiftModifier);
  }
}

void LogsDialog::mouseWheel()
{
  int orientation = 0;
  if (ui->ZoomX_ChkB->isChecked()) orientation |= Qt::Horizontal;
  if (ui->ZoomY_ChkB->isChecked()) orientation |= Qt::Vertical;
  if (orientation) axisRect->setRangeZoom((Qt::Orientation)orientation);
  else             axisRect->setRangeZoom(Qt::Horizontal | Qt::Vertical);
}

void LogsDialog::removeAllGraphs()
{
  ui->customPlot->clearGraphs();
  ui->customPlot->clearItems();
  ui->customPlot->legend->setVisible(false);
  rightLegend->clearItems();
  rightLegend->setVisible(false);
  axisRect->axis(QCPAxis::atRight)->setSelectedParts(QCPAxis::spNone);
  axisRect->axis(QCPAxis::atRight)->setVisible(false);
  axisRect->axis(QCPAxis::atLeft)->setSelectedParts(QCPAxis::spNone);
  axisRect->axis(QCPAxis::atLeft)->setTickLabels(false);
  axisRect->axis(QCPAxis::atLeft, 1)->setVisible(false);
  axisRect->axis(QCPAxis::atLeft, 1)->setSelectedParts(QCPAxis::spNone);
  axisRect->axis(QCPAxis::atRight, 1)->setVisible(false);
  axisRect->axis(QCPAxis::atRight, 1)->setSelectedParts(QCPAxis::spNone);
  axisRect->axis(QCPAxis::atBottom)->setSelectedParts(QCPAxis::spNone);
  ui->customPlot->replot();
  tracerMaxAlt = nullptr;
  cursorA = nullptr;
  cursorB = nullptr;
  cursorLine = nullptr;
  ui->labelCursors->setText("");
}

void LogsDialog::placeCursor(double x, bool second)
{
  QCPItemTracer *cursor = second ? cursorB : cursorA;
  if (cursor) {
    cursor->setGraphKey(x);
    cursor->updatePosition();
    cursor->setVisible(true);
  }
  if (cursorA && cursorB) updateCursorsLabel();
}

void LogsDialog::updateCursorsLabel()
{
  QString text = QString("Max Altitude: %1 m\n")
                     .arg(tracerMaxAlt->position->value(), 0, 'f', 1);
  if (cursorA->visible())
    text += tr("Cursor A: %1 m").arg(cursorA->position->value(), 0, 'f', 1) + "\n";
  if (cursorB->visible())
    text += tr("Cursor B: %1 m").arg(cursorB->position->value(), 0, 'f', 1) + "\n";
  if (cursorA->visible() && cursorB->visible()) {
    cursorLine->setVisible(true);
    double deltaX = cursorB->position->key() - cursorA->position->key();
    double deltaY = cursorB->position->value() - cursorA->position->value();
    double slope = (deltaX != 0) ? deltaY / deltaX : 0;
    text += tr("Time delta: %1").arg(formatTimeDelta(deltaX)) + "\n";
    text += tr("Climb rate: %1 m/s")
                .arg(slope, 0, 'f', fabs(slope) < 1.0 ? 2 : 1) + "\n";
  }
  ui->labelCursors->setText(text);
}

QString LogsDialog::formatTimeDelta(double timeDelta)
{
  if (abs(int(timeDelta)) < 10)
    return QString("%1 s").arg(timeDelta, 1, 'f', 1);
  int seconds = (int)round(fabs(timeDelta));
  int hours = seconds / 3600; seconds %= 3600;
  int minutes = seconds / 60; seconds %= 60;
  if (hours)   return QString("%1h:%2:%3").arg(hours)
      .arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
  if (minutes) return QString("%1m:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
  return QString("%1 s").arg(seconds);
}

void LogsDialog::yAxisChangeRanges(QCPRange range)
{
  if (axisRect->axis(QCPAxis::atRight)->visible()) {
    double lowerChange = (range.lower - yAxesRanges[firstLeft].min) * yAxesRatios[firstRight];
    double upperChange = (range.upper - yAxesRanges[firstLeft].max) * yAxesRatios[firstRight];
    yAxesRanges[firstRight].min += lowerChange;
    yAxesRanges[firstRight].max += upperChange;
    axisRect->axis(QCPAxis::atRight)->setRange(yAxesRanges[firstRight].min,
                                               yAxesRanges[firstRight].max);

    if (axisRect->axisCount(QCPAxis::atLeft) == 2) {
      lowerChange = (range.lower - yAxesRanges[firstLeft].min) * yAxesRatios[secondLeft];
      upperChange = (range.upper - yAxesRanges[firstLeft].max) * yAxesRatios[secondLeft];
      yAxesRanges[secondLeft].min += lowerChange;
      yAxesRanges[secondLeft].max += upperChange;
      axisRect->axis(QCPAxis::atLeft, 1)->setRange(yAxesRanges[secondLeft].min,
                                                   yAxesRanges[secondLeft].max);

      if (axisRect->axisCount(QCPAxis::atRight) == 2) {
        lowerChange = (range.lower - yAxesRanges[firstLeft].min) * yAxesRatios[secondRight];
        upperChange = (range.upper - yAxesRanges[firstLeft].max) * yAxesRatios[secondRight];
        yAxesRanges[secondRight].min += lowerChange;
        yAxesRanges[secondRight].max += upperChange;
        axisRect->axis(QCPAxis::atRight, 1)->setRange(yAxesRanges[secondRight].min,
                                                      yAxesRanges[secondRight].max);
      }
    }
    yAxesRanges[firstLeft].min = range.lower;
    yAxesRanges[firstLeft].max = range.upper;
  }
}

void LogsDialog::addMaxAltitudeMarker(const coords_t &c, QCPGraph *graph)
{
  int positionIndex = 0;
  double maxAlt = -100000;
  for (int i = 0; i < c.x.count(); ++i) {
    if (c.y.at(i) > maxAlt) { maxAlt = c.y.at(i); positionIndex = i; }
  }
  tracerMaxAlt = new QCPItemTracer(ui->customPlot);
  tracerMaxAlt->setGraph(graph);
  tracerMaxAlt->setInterpolating(true);
  tracerMaxAlt->setStyle(QCPItemTracer::tsSquare);
  tracerMaxAlt->setPen(QPen(Qt::blue));
  tracerMaxAlt->setBrush(Qt::NoBrush);
  tracerMaxAlt->setSize(7);
  tracerMaxAlt->setGraphKey(c.x.at(positionIndex));
  tracerMaxAlt->updatePosition();
}

void LogsDialog::countNumberOfThrows(const coords_t &, QCPGraph *) {}

void LogsDialog::addCursor(QCPItemTracer **cursor, QCPGraph *graph, const QColor &color)
{
  QCPItemTracer *c = new QCPItemTracer(ui->customPlot);
  c->setGraph(graph);
  c->setInterpolating(false);
  c->setStyle(QCPItemTracer::tsCrosshair);
  QPen p(color); p.setStyle(Qt::DashLine);
  c->setPen(p);
  c->setBrush(color);
  c->setSize(7);
  c->setVisible(false);
  *cursor = c;
}

void LogsDialog::addCursorLine(QCPItemStraightLine **line, QCPGraph *, const QColor &color)
{
  QCPItemStraightLine *l = new QCPItemStraightLine(ui->customPlot);
  l->point1->setParentAnchor(cursorA->position);
  l->point2->setParentAnchor(cursorB->position);
  QPen p(color); p.setStyle(Qt::DashLine);
  l->setPen(p);
  l->setVisible(false);
  *line = l;
}