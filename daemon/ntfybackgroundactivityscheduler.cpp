// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfybackgroundactivityscheduler.h"

#include <backgroundactivity.h>

NtfyBackgroundActivityScheduler::NtfyBackgroundActivityScheduler(QObject *parent)
    : NtfyWakeupScheduler(parent)
    , m_activity(new BackgroundActivity(this))
{
    // While running, BackgroundActivity keeps the device out of suspend until we wait() again.
    connect(m_activity, &BackgroundActivity::running, this, &NtfyWakeupScheduler::wokeUp);
}

void NtfyBackgroundActivityScheduler::wait(int seconds)
{
    // Our intervals are all BackgroundActivity::Frequency slots.
    m_activity->wait(static_cast<BackgroundActivity::Frequency>(seconds));
}

void NtfyBackgroundActivityScheduler::stop()
{
    m_activity->stop();
}
