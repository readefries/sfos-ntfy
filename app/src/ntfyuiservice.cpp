// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfyuiservice.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QQuickView>

namespace {
const QString ServiceName = QStringLiteral("nl.readefries.harbourntfy.ui");
} // namespace

NtfyUiService::NtfyUiService(QQuickView *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
{
    new NtfyMeegoLaunchAdaptor(this);
}

bool NtfyUiService::registerOnSessionBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // The launcher calls com.nokia.MApplicationIf.launch on MeeGo's fixed path, whatever the
    // desktop file says.
    return bus.registerObject(QStringLiteral("/"), this,
                              QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportAdaptors)
            && bus.registerObject(QStringLiteral("/org/maemo/m"), this, QDBusConnection::ExportAdaptors)
            && bus.registerService(ServiceName);
}

bool NtfyUiService::otherInstanceRunning()
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(ServiceName);
}

void NtfyUiService::activateOtherInstance()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(ServiceName, QStringLiteral("/"), ServiceName,
                                                             QStringLiteral("activate"));
    QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
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

NtfyMeegoLaunchAdaptor::NtfyMeegoLaunchAdaptor(NtfyUiService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
}

void NtfyMeegoLaunchAdaptor::launch()
{
    m_service->activate();
}

void NtfyMeegoLaunchAdaptor::launch(const QStringList &)
{
    m_service->activate();
}
