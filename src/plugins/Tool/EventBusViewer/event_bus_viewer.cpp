/**
 * Event Bus Viewer — v2 IPluginTool plugin
 *
 * Registers a single tool ("event_bus_viewer") that, when invoked from the
 * Tools menu, opens a Qt dialog listing the most recent events dispatched on
 * the engine's EventBus.
 *
 * The plugin does NOT link the engine statically: doing so would create a
 * second EventBus singleton inside the plugin (empty — it never receives
 * events). Instead it reaches the host's EventBus singleton through the
 * executable's exported symbols (the gamemodmanager binary is linked with
 * -rdynamic, exactly like the gmm_* C-ABI accessors in abi_bridge.cpp). The
 * plugin only needs the EventBus declaration from the engine headers; the
 * definition resolves at dlopen time.
 *
 * Build: shared MODULE, links only Qt6::Widgets.
 */

#include "gmm_abi_v2.h"

#include "core/events/event_bus.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialog>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <chrono>
#include <string>
#include <vector>

namespace {

// Invoked when the user launches the tool from the Tools menu. Runs on the
// calling (UI) thread, so showing a modal dialog is safe.
void event_bus_viewer_invoke(void *user_data) {
  (void)user_data;

  const std::vector<engine::EventRecord> events =
      engine::EventBus::instance().recent_events();

  QDialog dlg;
  dlg.setWindowTitle(QObject::tr("Event Bus Viewer"));
  dlg.resize(760, 480);

  auto *layout = new QVBoxLayout(&dlg);

  auto *table = new QTableWidget(static_cast<int>(events.size()), 3, &dlg);
  table->setHorizontalHeaderLabels(
      {QObject::tr("Event"), QObject::tr("Payload"), QObject::tr("Timestamp")});
  table->horizontalHeader()->setStretchLastSection(true);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->verticalHeader()->setVisible(false);

  for (int i = 0; i < table->rowCount(); ++i) {
    const engine::EventRecord &e = events[i];
    const qint64 ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          e.timestamp.time_since_epoch())
                          .count();
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);

    table->setItem(i, 0,
                   new QTableWidgetItem(QString::fromStdString(e.event_id)));
    table->setItem(i, 1,
                   new QTableWidgetItem(QString::fromStdString(e.payload)));
    table->setItem(i, 2, new QTableWidgetItem(dt.toString(Qt::ISODateWithMs)));
  }
  table->resizeColumnsToContents();

  layout->addWidget(table);

  auto *close_btn = new QPushButton(QObject::tr("Close"), &dlg);
  QObject::connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
  layout->addWidget(close_btn);

  dlg.exec();
}

} // namespace

extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v2(GmmRegistrationCtxV2 *ctx) {
  if (!ctx)
    return;

  GmmPluginInfo info{};
  info.name = "Event Bus Viewer";
  info.author = "GMM";
  info.version = "1.0";
  info.description = "View event bus activity";

  if (ctx->register_plugin)
    ctx->register_plugin(ctx, info);

  if (ctx->register_tool) {
    ctx->register_tool(ctx, "event_bus_viewer", "tool", event_bus_viewer_invoke,
                       nullptr);
  }
}

} // extern "C"
