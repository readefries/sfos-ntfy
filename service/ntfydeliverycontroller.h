// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYDELIVERYCONTROLLER_H
#define NTFYDELIVERYCONTROLLER_H

#include <QList>
#include <QObject>
#include <QTimer>

class NtfySubscriptionManager;

// Periodic wake-ups while the device is suspended. On Sailfish OS this is Nemo.KeepAlive's
// BackgroundActivity (see daemon/); tests use a fake.
class NtfyWakeupScheduler : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Wake up again in about this many seconds, which is one of the system's global wake-up
    // slots, so that it coincides with other apps' wake-ups. Until the next wakeUp() the device
    // may suspend; while handling one it stays awake, until wait() or stop() is called.
    virtual void wait(int seconds) = 0;
    virtual void stop() = 0;

signals:
    void wokeUp();
};

// Applies the user's choice of how messages get delivered while the phone sleeps:
//  - Instant: keep the streams up, waking up every 30 s or 2.5 min to revive dead ones.
//  - Balanced: stream while awake, reconnect (and so catch up) every 5, 10 or 15 min.
//  - Battery saver: no permanent connection; poll every 15, 30 or 60 min.
// Nothing gets lost in any mode: every wake-up resumes where the last one stopped.
class NtfyDeliveryController : public QObject
{
    Q_OBJECT
public:
    enum Mode {
        Instant,
        Balanced,
        BatterySaver
    };
    Q_ENUM(Mode)

    struct Stats {
        int wakeups = 0;
        qint64 lastWakeup = 0; // unix time, 0 if none yet
        int messages = 0;
        qint64 totalDelay = 0; // seconds between publishing and arrival, summed
        qint64 maxDelay = 0;
    };

    NtfyDeliveryController(NtfySubscriptionManager *manager, NtfyWakeupScheduler *scheduler,
                           QObject *parent = nullptr);

    // Loads the stored settings, starts the manager and schedules the first wake-up.
    void start();

    Mode mode() const { return m_mode; }
    int interval() const { return m_interval; }
    bool setDelivery(Mode mode, int intervalSeconds, QString *error = nullptr);

    Stats stats() const { return m_stats; }

    static QList<int> intervals(Mode mode);
    static int defaultInterval(Mode mode);
    static QString modeName(Mode mode);
    static bool modeFromName(const QString &name, Mode *mode);

    // For tests. awakeLimit caps how long one wake-up may keep the device up; settle is how long
    // to let pending network data arrive before deciding what to do / after reconnecting.
    void setTimings(int awakeLimitMsecs, int settleMsecs);

signals:
    void deliveryChanged();

private:
    void apply();
    void onWokeUp();
    void checkStreams();
    void onStreamStateChanged();
    void finishWakeup();

    NtfySubscriptionManager *m_manager;
    NtfyWakeupScheduler *m_scheduler;
    Mode m_mode = Balanced;
    int m_interval;
    bool m_awake = false;
    bool m_waitingForStreams = false;
    QTimer m_awakeLimit;
    QTimer m_settle;
    int m_settleMsecs;
    Stats m_stats;
};

#endif // NTFYDELIVERYCONTROLLER_H
