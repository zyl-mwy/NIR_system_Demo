#include "Database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>

DatabaseManager::DatabaseManager() {}
DatabaseManager::~DatabaseManager() {}

void DatabaseManager::initialize()
{
    if (db.isValid() && db.isOpen()) return;
    db = QSqlDatabase::addDatabase("QSQLITE");
    QString baseDir = QCoreApplication::applicationDirPath();
    db.setDatabaseName(baseDir + "/../data/runtime.sqlite");
    db.open();
    createTablesIfNotExists();
}

void DatabaseManager::createTablesIfNotExists()
{
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS spectra ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts TEXT NOT NULL,"
           "wavelengths_json TEXT NOT NULL,"
           "raw_spectrum_json TEXT NOT NULL"
           ")");
    q.exec("CREATE TABLE IF NOT EXISTS predictions ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts TEXT NOT NULL,"
           "results_json TEXT NOT NULL"
           ")");
    q.exec("CREATE TABLE IF NOT EXISTS prediction_status ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts TEXT NOT NULL,"
           "property TEXT NOT NULL,"
           "value REAL NOT NULL,"
           "min REAL,"
           "max REAL,"
           "status TEXT NOT NULL"
           ")");
}

void DatabaseManager::insertSpectrumRecord(const QJsonArray &wavelengths, const QJsonArray &rawSpectrum)
{
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT INTO spectra (ts, wavelengths_json, raw_spectrum_json) VALUES (:ts, :w, :s)");
    q.bindValue(":ts", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
    q.bindValue(":w", QJsonDocument(wavelengths).toJson(QJsonDocument::Compact));
    q.bindValue(":s", QJsonDocument(rawSpectrum).toJson(QJsonDocument::Compact));
    q.exec();
}

void DatabaseManager::insertPredictionRecord(const QMap<QString, float>& results,
                                             const QMap<QString, QPair<float, float>>& thresholdRanges,
                                             const std::function<QString(const QString&)>& normalizeKeyFn)
{
    if (!db.isOpen()) return;
    QJsonObject resObj;
    for (auto it = results.begin(); it != results.end(); ++it) resObj[it.key()] = it.value();
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO predictions (ts, results_json) VALUES (:ts, :r)");
        q.bindValue(":ts", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
        q.bindValue(":r", QJsonDocument(resObj).toJson(QJsonDocument::Compact));
        q.exec();
    }
    for (auto it = results.begin(); it != results.end(); ++it) {
        const QString key = normalizeKeyFn(it.key());
        const float val = it.value();
        auto range = thresholdRanges.value(key, qMakePair(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()));
        const bool ok = (val >= range.first && val <= range.second);
        QSqlQuery q(db);
        q.prepare("INSERT INTO prediction_status (ts, property, value, min, max, status) VALUES (:ts, :p, :v, :min, :max, :st)");
        q.bindValue(":ts", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
        q.bindValue(":p", it.key());
        q.bindValue(":v", val);
        q.bindValue(":min", range.first);
        q.bindValue(":max", range.second);
        q.bindValue(":st", ok ? "NORMAL" : "ALARM");
        q.exec();
    }
}


