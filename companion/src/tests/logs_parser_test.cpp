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

#include "gtests.h"
#include "logs_parser.h"
#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>


class TempFile
{
public:
  TempFile(const QString& contents, const QString& name = "test.csv")
  {
    if (!_dir.isValid()) {
      ADD_FAILURE() << "Cannot create temporary directory for test";
      return;
    }
    _path = _dir.filePath(name);

    QFile file(_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      ADD_FAILURE() << "Cannot create test CSV file: " << _path.toStdString();
      _path.clear();
      return;
    }
    QTextStream stream(&file);
    stream << contents;
    stream.flush();
  }

  QString path() const {
    return _path;
  }

private:
  QTemporaryDir _dir;
  QString _path;
};


TEST(LogsParser, ParseSimpleLog)
{
  TempFile csv("Date,Time,RSSI,Alt\n"
               "2024-01-01,10:00:00.000,-50,100\n"
               "2024-01-01,10:00:00.100,-51,101\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_EQ(result.data.row_count(), 2);
  EXPECT_EQ(result.data.col_count(), 4);

  ASSERT_EQ(result.data.column_names.size(), 4);
  EXPECT_EQ(result.data.column_names.at(0), QString("Date"));
  EXPECT_EQ(result.data.column_names.at(1), QString("Time"));
  EXPECT_EQ(result.data.column_names.at(2), QString("RSSI"));
  EXPECT_EQ(result.data.column_names.at(3), QString("Alt"));

  EXPECT_FALSE(result.data.fields.at(0).is_numeric);
  EXPECT_FALSE(result.data.fields.at(1).is_numeric);
  EXPECT_TRUE(result.data.fields.at(2).is_numeric);
  EXPECT_TRUE(result.data.fields.at(3).is_numeric);

  ASSERT_EQ(result.data.fields.at(2).numbers.size(), 2);
  EXPECT_DOUBLE_EQ(result.data.fields.at(2).numbers.at(0), -50.0);
  EXPECT_DOUBLE_EQ(result.data.fields.at(2).numbers.at(1), -51.0);
  EXPECT_DOUBLE_EQ(result.data.fields.at(3).numbers.at(0), 100.0);
  EXPECT_DOUBLE_EQ(result.data.fields.at(3).numbers.at(1), 101.0);

  ASSERT_EQ(result.data.timestamps.size(), 2);
  EXPECT_EQ(result.data.timestamps.at(1) - result.data.timestamps.at(0), 100);

  EXPECT_EQ(result.data.errors, 0);
  EXPECT_EQ(result.data.lines, 2);
  EXPECT_EQ(result.data.filename, QString("test"));
}


TEST(LogsParser, DetectNumericOrString)
{
  TempFile csv("Date,Time,Name,Val\n"
               "2024-01-01,10:00:00.000,name1,1\n"
               "2024-01-01,10:00:00.100,name2,2\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_EQ(result.data.col_count(), 4);

  EXPECT_FALSE(result.data.fields.at(2).is_numeric);
  ASSERT_EQ(result.data.fields.at(2).strings.size(), 2);
  EXPECT_EQ(result.data.fields.at(2).strings.at(0), QString("name1"));
  EXPECT_EQ(result.data.fields.at(2).strings.at(1), QString("name2"));

  EXPECT_TRUE(result.data.fields.at(3).is_numeric);
  EXPECT_DOUBLE_EQ(result.data.fields.at(3).numbers.at(0), 1.0);
  EXPECT_DOUBLE_EQ(result.data.fields.at(3).numbers.at(1), 2.0);
}


TEST(LogsParser, RejectNonLogHeader)
{
  TempFile csv("Field1,Field2\n"
               "1,2\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_TRUE(result.error.startsWith("Invalid format:"));
  EXPECT_TRUE(result.data.is_empty());
}


TEST(LogsParser, RejectMissingFile)
{
  auto result = LogsParser::parse_file("/123/123/does_not_exist.csv", {}, {});

  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_TRUE(result.error.startsWith("Cannot open file:"));
  EXPECT_TRUE(result.data.is_empty());
}


TEST(LogsParser, RejectEmptyData)
{
  TempFile csv("Date,Time,RSSI\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(result.error, QString("Log file contains no data"));
  EXPECT_TRUE(result.data.is_empty());
}


TEST(LogsParser, CountInvalidRows)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "no data\n"
               "2024-01-01,10:00:00.100,-51\n"
               "2024-01-01,10:00:00.200,-52,extra\n"
               "2024-13-01,10:00:00.300,-53\n"
               "2024-01-01,25:00:00.000,-54\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_EQ(result.data.row_count(), 2);
  EXPECT_EQ(result.data.errors, 4);
  EXPECT_EQ(result.data.lines, 6);
}


TEST(LogsParser, SkipEmptyRows)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "\n"
               "2024-01-01,10:00:00.100,-51\n"
               "\n"
               "2024-01-01,10:00:00.200,-52\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_EQ(result.data.row_count(), 3);
  EXPECT_EQ(result.data.errors, 0);
  EXPECT_EQ(result.data.lines, 3);
}


TEST(LogsParser, ResolveTypeOnFirstValidRow)
{
  TempFile csv("Date,Time,RSSI\n"
               "no data\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:00.100,-51\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_EQ(result.data.row_count(), 2);
  EXPECT_EQ(result.data.errors, 1);
  EXPECT_TRUE(result.data.fields.at(2).is_numeric);
}


TEST(LogsParser, SingleSession)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:00.100,-51\n"
               "2024-01-01,10:00:00.200,-52\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  ASSERT_EQ(result.data.session_breaks.size(), 1);
  EXPECT_EQ(result.data.session_breaks.at(0), 2);
}


TEST(LogsParser, DetectSessions)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:00.100,-51\n"
               "2024-01-01,10:05:00.000,-52\n"
               "2024-01-01,10:05:00.100,-53\n"
               "2024-01-01,10:10:00.000,-54\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  ASSERT_EQ(result.data.session_breaks.size(), 3);
  EXPECT_EQ(result.data.session_breaks.at(0), 1);
  EXPECT_EQ(result.data.session_breaks.at(1), 3);
  EXPECT_EQ(result.data.session_breaks.at(2), 4);
}


TEST(LogsParser, NoSessionBreak)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:30.000,-51\n"
               "2024-01-01,10:01:00.000,-52\n");

  auto result = LogsParser::parse_file(csv.path(), {}, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  ASSERT_EQ(result.data.session_breaks.size(), 1);
  EXPECT_EQ(result.data.session_breaks.at(0), 2);
}


TEST(LogsParser, CancelParsing)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:00.100,-51\n");

  auto cancel_func = []() { return true; };
  auto result = LogsParser::parse_file(csv.path(), {}, cancel_func);

  EXPECT_EQ(result.error, QString("cancelled"));
}


TEST(LogsParser, ProgressReachesHundred)
{
  TempFile csv("Date,Time,RSSI\n"
               "2024-01-01,10:00:00.000,-50\n"
               "2024-01-01,10:00:00.100,-51\n");

  int last_progress = -1;
  int count = 0;

  auto progress_func = [&](int p) {
    ++count;
    last_progress = p;
  };

  auto result = LogsParser::parse_file(csv.path(), progress_func, {});

  ASSERT_TRUE(result.error.isEmpty()) << result.error.toStdString();
  EXPECT_GE(count, 1);
  EXPECT_EQ(last_progress, 100);
}