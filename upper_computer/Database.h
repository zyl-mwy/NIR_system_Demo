#pragma once

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    void initialize();
    void createTablesIfNotExists();

    void insertSpectrumRecord(const QJsonArray &wavelengths, const QJsonArray &rawSpectrum);
    void insertPredictionRecord(const QMap<QString, float>& results,
                                const QMap<QString, QPair<float, float>>& thresholdRanges,
                                const std::function<QString(const QString&)>& normalizeKeyFn);

private:
    QSqlDatabase db;
};


