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
#include "logsparser.h"

namespace LogParser {

static bool looksNumeric(const QString& s) {
  if (s.isEmpty()) return false;
  bool ok = false;
  s.toDouble(&ok);
  return ok;
}

Result parseFile(const QString& fileName,
                 std::function<void(int)> progress,
                 std::function<bool()>   cancelCheck)
{
  Result res;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    res.error = QString("Cannot open file %1").arg(fileName);
    return res;
  }

  QString header = file.readLine();
  if (!header.startsWith("Date,Time")) {
    res.error = QString("Not a valid EdgeTX log file: %1").arg(fileName);
    return res;
  }
  res.data.columnNames = header.trimmed().split(',');

  const int numFields = res.data.columnNames.size();
  res.data.fields.resize(numFields);
  for (int j = 0; j < numFields; ++j) {
    res.data.fields[j].name = res.data.columnNames.at(j);
  }

  bool typesResolved = false;
  const qint64 fileSize = file.size();
  int errors = 0;
  int lines = -1;

  while (!file.atEnd()) {
    if ((lines & 0x3FF) == 0 && cancelCheck && cancelCheck()) {
      res.error = QString("cancelled");
      return res;
    }

    QString line = file.readLine().trimmed();
    if (line.isEmpty()) continue;

    QStringList cols = line.split(',');
    if (cols.size() != numFields) {
      errors++;
      lines++;
      continue;
    }

    if (!typesResolved) {
      for (int j = 0; j < numFields; ++j) {
        if (j < 2) {
          res.data.fields[j].isNumeric = false;
        }
        else {
          res.data.fields[j].isNumeric = looksNumeric(cols.at(j));
        }
      }
      typesResolved = true;
    }

    QStringList sDate = cols.at(0).split("-");
    QStringList sTime = cols.at(1).split(":");
    QStringList sSecs;
    if (sTime.size() == 3) {
      sSecs = sTime.at(2).split(".");
      if (sSecs.size() == 1) {
        sSecs.append("0");
      }
    }
    if (sDate.size() != 3 || sTime.size() != 3 || sSecs.size() != 2) {
      errors++;
      lines++;
      continue;
    }

    QDate d(sDate.at(0).toInt(), sDate.at(1).toInt(), sDate.at(2).toInt());
    QTime t(sTime.at(0).toInt(), sTime.at(1).toInt(),
            sSecs.at(0).toInt(), sSecs.at(1).toInt());
    if (!d.isValid() || !t.isValid()) {
      errors++;
      lines++;
      continue;
    }

    res.data.timestamps.append(QDateTime(d, t).toMSecsSinceEpoch());

    for (int j = 0; j < numFields; ++j) {
      LogField& f = res.data.fields[j];
      if (f.isNumeric) {
        f.num.append(cols.at(j).toDouble());
      }
      else {
        f.str.append(cols.at(j));
      }
    }

    lines++;
    if ((lines & 0x3FF) == 0 && progress && fileSize > 0) 
      progress(int(file.pos() * 100 / fileSize));
  }

  if (progress) progress(100);

  if (res.data.timestamps.isEmpty()) {
    res.error = QString("Log file contains no data");
    return res;
  }

  res.data.logFilename = QFileInfo(fileName).baseName();
  res.data.errors = errors;
  res.data.lines = lines + 1;

  const int n = res.data.timestamps.size();

  QDateTime last = QDateTime::fromMSecsSinceEpoch(res.data.timestamps.at(0));
  for (int i = 1; i < n; ++i) {
    QDateTime cur = QDateTime::fromMSecsSinceEpoch(res.data.timestamps.at(i));
    if (last.secsTo(cur) > 60) {
      res.data.sessionBreaks.append(i - 1);
    }
    last = cur;
  }
  res.data.sessionBreaks.append(n - 1);

  res.ok = true;
  return res;
}

} // namespace LogParser
