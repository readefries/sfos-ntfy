// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYBACKGROUNDACTIVITYSCHEDULER_H
#define NTFYBACKGROUNDACTIVITYSCHEDULER_H

#include "ntfydeliverycontroller.h"

class BackgroundActivity;

// Wake-ups through Nemo.KeepAlive, aligned with the system-wide wake-up slots (iphb), so they
// even fire from late suspend and coincide with other apps' wake-ups.
class NtfyBackgroundActivityScheduler : public NtfyWakeupScheduler
{
    Q_OBJECT
public:
    explicit NtfyBackgroundActivityScheduler(QObject *parent = nullptr);

    void wait(int seconds) override;
    void stop() override;

private:
    BackgroundActivity *m_activity;
};

#endif // NTFYBACKGROUNDACTIVITYSCHEDULER_H
