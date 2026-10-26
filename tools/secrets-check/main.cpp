// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Checks that NtfySecretsCredentialStore works against the device's secrets daemon.
// Takes no arguments on purpose: sailfish-secretsd identifies callers by their command line.
// Run it twice from the same directory with the same command (e.g. ./secrets-check): the
// second run also checks that the value from the first run persisted.

#include "ntfysecretscredentialstore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

int failures = 0;

void check(bool ok, const QString &what, const QString &error = QString())
{
    out() << (ok ? "ok    " : "FAIL  ") << what;
    if (!ok && !error.isEmpty())
        out() << ": " << error;
    out() << endl;
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc > 1) {
        out() << "secrets-check takes no arguments (they would change its identity)" << endl;
        return 2;
    }

    QFile cmdline(QStringLiteral("/proc/self/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly))
        out() << "identity (cmdline): " << QString::fromLocal8Bit(cmdline.readAll().replace('\0', ' ')).trimmed() << endl;

    NtfySecretsCredentialStore store(QStringLiteral("harbourntfysecretscheck"));
    const QString key = QStringLiteral("selftest");
    const QString persistKey = QStringLiteral("persisted");
    QString error;
    QByteArray value;
    bool found = false;

    bool ok = store.load(persistKey, &value, &found, &error);
    check(ok, QStringLiteral("load value from a previous run"), error);
    if (ok) {
        out() << "      "
              << (found ? QStringLiteral("found, stored at %1").arg(QString::fromUtf8(value))
                        : QStringLiteral("none (first run?)"))
              << endl;
    }

    error.clear();
    check(store.store(key, "one", &error), QStringLiteral("store"), error);
    error.clear();
    ok = store.load(key, &value, &found, &error);
    check(ok && found && value == "one", QStringLiteral("load what was stored"), error);

    error.clear();
    check(store.store(key, "two", &error), QStringLiteral("overwrite"), error);
    error.clear();
    ok = store.load(key, &value, &found, &error);
    check(ok && found && value == "two", QStringLiteral("load overwritten value"), error);

    error.clear();
    check(store.remove(key, &error), QStringLiteral("remove"), error);
    error.clear();
    ok = store.load(key, &value, &found, &error);
    check(ok && !found, QStringLiteral("removed value is gone"), error);
    error.clear();
    check(store.remove(key, &error), QStringLiteral("removing again succeeds"), error);

    error.clear();
    const QByteArray now = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
    check(store.store(persistKey, now, &error), QStringLiteral("store value for the next run"), error);

    out() << (failures ? QStringLiteral("%1 check(s) failed").arg(failures) : QStringLiteral("all checks passed"))
          << endl;
    return failures ? 1 : 0;
}
