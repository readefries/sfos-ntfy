// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfyuiservice.h"

#include <QDBusConnection>
#include <QQuickView>

namespace {
const QString ServiceName = QStringLiteral("nl.readefries.harbourntfy.ui");
} // namespace

NtfyUiService::NtfyUiService(QQuickView *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
{
}

bool NtfyUiService::registerOnSessionBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.registerObject(QStringLiteral("/"), this, QDBusConnection::ExportScriptableSlots)
            && bus.registerService(ServiceName);
}

void NtfyUiService::openSubscription(qlonglong subscriptionId)
{
    showWindow();
    emit subscriptionRequested(subscriptionId);
}

void NtfyUiService::activate()
{
    showWindow();
    emit activationRequested();
}

void NtfyUiService::showWindow()
{
    if (!m_view->isVisible())
        m_view->show();
    m_view->raise();
    m_view->requestActivate();
}
