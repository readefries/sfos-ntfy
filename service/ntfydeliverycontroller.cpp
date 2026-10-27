// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfydeliverycontroller.h"

#include "ntfysubscriptionmanager.h"

#include <QDateTime>

namespace {
const QString ModeKey = QStringLiteral("delivery/mode");
const QString IntervalKey = QStringLiteral("delivery/interval");

const int DefaultAwakeLimit = 30 * 1000;
const int DefaultSettle = 2 * 1000;
// ntfy sends a keepalive every 45 s; longer silence on a "connected" stream means it's dead.
const qint64 MaxStreamSilence = 60 * 1000;
} // namespace

NtfyDeliveryController::NtfyDeliveryController(NtfySubscriptionManager *manager, NtfyWakeupScheduler *scheduler,
                                               QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_scheduler(scheduler)
    , m_interval(defaultInterval(Balanced))
    , m_settleMsecs(DefaultSettle)
{
    m_awakeLimit.setSingleShot(true);
    m_awakeLimit.setInterval(DefaultAwakeLimit);
    connect(&m_awakeLimit, &QTimer::timeout, this, &NtfyDeliveryController::finishWakeup);
    m_settle.setSingleShot(true);

    connect(m_scheduler, &NtfyWakeupScheduler::wokeUp, this, &NtfyDeliveryController::onWokeUp);
    connect(m_manager, &NtfySubscriptionManager::pollFinished, this, [this]() {
        if (m_awake && m_mode == BatterySaver)
            finishWakeup();
    });
    connect(m_manager, &NtfySubscriptionManager::serverStateChanged, this,
            &NtfyDeliveryController::onStreamStateChanged);
    connect(m_manager, &NtfySubscriptionManager::messageReceived, this,
            [this](const NtfySubscriptionRecord &, const NtfyStoredMessage &message) {
        const qint64 delay = QDateTime::currentMSecsSinceEpoch() / 1000 - message.event.time;
        if (delay < 0)
            return; // clocks disagree
        ++m_stats.messages;
        m_stats.totalDelay += delay;
        m_stats.maxDelay = qMax(m_stats.maxDelay, delay);
    });
}

QList<int> NtfyDeliveryController::intervals(Mode mode)
{
    switch (mode) {
    case Instant:
        return QList<int>() << 30 << 150;
    case Balanced:
        return QList<int>() << 300 << 600 << 900;
    case BatterySaver:
        return QList<int>() << 900 << 1800 << 3600;
    }
    return QList<int>();
}

int NtfyDeliveryController::defaultInterval(Mode mode)
{
    switch (mode) {
    case Instant:
        return 150;
    case Balanced:
        return 300;
    case BatterySaver:
        return 1800;
    }
    return 300;
}

QString NtfyDeliveryController::modeName(Mode mode)
{
    switch (mode) {
    case Instant:
        return QStringLiteral("instant");
    case Balanced:
        return QStringLiteral("balanced");
    case BatterySaver:
        return QStringLiteral("battery-saver");
    }
    return QString();
}

bool NtfyDeliveryController::modeFromName(const QString &name, Mode *mode)
{
    for (Mode candidate : {Instant, Balanced, BatterySaver}) {
        if (modeName(candidate) == name) {
            *mode = candidate;
            return true;
        }
    }
    return false;
}

void NtfyDeliveryController::setTimings(int awakeLimitMsecs, int settleMsecs)
{
    m_awakeLimit.setInterval(awakeLimitMsecs);
    m_settleMsecs = settleMsecs;
}

void NtfyDeliveryController::start()
{
    NtfyDatabase *database = m_manager->database();
    Mode mode = Balanced;
    if (!modeFromName(database->setting(ModeKey), &mode))
        mode = Balanced;
    const int interval = database->setting(IntervalKey).toInt();

    m_mode = mode;
    m_interval = intervals(mode).contains(interval) ? interval : defaultInterval(mode);
    m_manager->start();
    apply();
}

bool NtfyDeliveryController::setDelivery(Mode mode, int intervalSeconds, QString *error)
{
    if (!intervals(mode).contains(intervalSeconds)) {
        if (error) {
            QStringList allowed;
            for (int seconds : intervals(mode))
                allowed.append(QString::number(seconds));
            *error = QStringLiteral("interval for %1 must be one of %2 seconds")
                    .arg(modeName(mode), allowed.join(QStringLiteral(", ")));
        }
        return false;
    }

    NtfyDatabase *database = m_manager->database();
    if (!database->setSetting(ModeKey, modeName(mode))
            || !database->setSetting(IntervalKey, QString::number(intervalSeconds))) {
        if (error)
            *error = database->lastError();
        return false;
    }

    m_mode = mode;
    m_interval = intervalSeconds;
    apply();
    emit deliveryChanged();
    return true;
}

void NtfyDeliveryController::apply()
{
    if (m_awake)
        finishWakeup(); // schedules with the new interval
    else
        m_scheduler->wait(m_interval);

    m_manager->setStreaming(m_mode != BatterySaver);
    if (m_mode == BatterySaver)
        m_manager->pollAll(); // catch up now rather than at the first wake-up
}

void NtfyDeliveryController::onWokeUp()
{
    m_awake = true;
    ++m_stats.wakeups;
    m_stats.lastWakeup = QDateTime::currentMSecsSinceEpoch() / 1000;
    m_awakeLimit.start();

    switch (m_mode) {
    case BatterySaver:
        // Finishes on pollFinished(), also when a poll was still running from before.
        m_manager->pollAll();
        break;
    case Balanced:
        // A fresh connection resumes from the last message and so delivers whatever was missed.
        m_manager->reconnectAll();
        m_waitingForStreams = true;
        onStreamStateChanged();
        break;
    case Instant:
        // Let data that queued up while we slept come in before judging the streams.
        m_settle.disconnect();
        connect(&m_settle, &QTimer::timeout, this, &NtfyDeliveryController::checkStreams);
        m_settle.start(m_settleMsecs);
        break;
    }
}

void NtfyDeliveryController::checkStreams()
{
    if (!m_awake)
        return;
    if (m_manager->reconnectStale(MaxStreamSilence) == 0) {
        finishWakeup();
        return;
    }
    m_waitingForStreams = true;
    onStreamStateChanged();
}

void NtfyDeliveryController::onStreamStateChanged()
{
    if (!m_awake || !m_waitingForStreams || !m_manager->allConnected())
        return;

    // Connected: give the catch-up messages a moment to arrive.
    m_waitingForStreams = false;
    m_settle.disconnect();
    connect(&m_settle, &QTimer::timeout, this, &NtfyDeliveryController::finishWakeup);
    m_settle.start(m_settleMsecs);
}

void NtfyDeliveryController::finishWakeup()
{
    if (!m_awake)
        return;
    m_awake = false;
    m_waitingForStreams = false;
    m_awakeLimit.stop();
    m_settle.stop();
    m_scheduler->wait(m_interval);
}
