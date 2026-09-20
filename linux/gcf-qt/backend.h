#pragma once
// C++ backend (gcf) bilan aloqa — WPF dagi Backend.cs analogi.
// GUI backend ni PATH (/usr/bin/gcf), o'z papkasi yoki yonidagi build dan topadi.
#include <QString>
#include <QStringList>

class Backend {
public:
    // Backend topilmasa false. Odatda startda bir marta chaqiriladi.
    static bool init(QString *error = nullptr);
    static QString exePath();

    struct Result { bool ok = false; QString output; };

    // Sinxron ishga tushirish (kichik buyruqlar: gpus/list/info/test uchun).
    // QStringList varianti xavfsiz (shell siz exec) — har doim shuni ishlating.
    static Result run(const QStringList &args, int timeoutMs = 120000);
    // ESKI: faqat sodda test/info uchun. Yo'llarda bo'sh joy/quote bo'lsa bo'linishi
    // mumkin — yangi kodda ishlatmang (QStringList ga o'ting).
    Q_DECL_DEPRECATED_X("Use run(QStringList) — shell siz, xavfsiz")
    static Result run(const QString &argLine, int timeoutMs = 120000);

    // Uzun javobdan oxirgi mazmunli qator (WPF dagi LastLine analogi).
    static QString lastLine(const QString &output);

    // "Siqildi: 110000 -> 54568 B ..." dan qisqa xulosa yasaydi.
    static QString shortSummary(const QString &line);
};
