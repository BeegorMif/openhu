#pragma once

#include <QCoreApplication>
#include <QTranslator>

// The texts of OpenHU in English, as in the shell: the code has text IDs only (docs/translations.md).
// Tests that look at messages call it in initTestCase().
inline void loadEnglish()
{
    static QTranslator translator;
    if(translator.isEmpty() && translator.load(OPENHU_QM_DIR "/openhu_en.qm"))
    {
        QCoreApplication::installTranslator(&translator);
    }
}
