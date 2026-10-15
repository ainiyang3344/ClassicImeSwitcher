#pragma once
#include <Windows.h>
#include <objbase.h>
#include <QString>
#include <QVariantMap>

struct InputProfile {
    QString id;
    QString name;
    QString languageName;
    QString klid;
    LANGID language = 0;
    CLSID clsid{};
    GUID profile{};
    HKL layout = nullptr;
    bool keyboard = false;
    bool enabled = false;
    bool systemDefault = false;

    bool isUS() const;
    bool isChinese() const;
    QVariantMap toVariant() const;
    static InputProfile parse(const QString &id, const QString &name = {});
};

QString guidString(const GUID &guid);
QString layoutString(HKL layout);
