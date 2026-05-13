#include "multistream-dock.hpp"
#include "destination-dialog.hpp"
#include "../config.hpp"
#include "../multistream-manager.hpp"
#include "../plugin-support.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMetaObject>
#include <QLabel>

MultistreamDock::MultistreamDock(QWidget *parent) : QFrame(parent)
{
	setObjectName("SceneMultistreamDock");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);

	auto *title = new QLabel(QString::fromUtf8(obs_module_text("DockTitle")), this);
	title->setStyleSheet("font-weight: bold;");
	layout->addWidget(title);

	table_ = new QTableWidget(this);
	table_->setColumnCount(7);
	table_->setHorizontalHeaderLabels({
		QString::fromUtf8(obs_module_text("Col.Enabled")),
		QString::fromUtf8(obs_module_text("Col.Name")),
		QString::fromUtf8(obs_module_text("Col.Scene")),
		QString::fromUtf8(obs_module_text("Col.Resolution")),
		QString::fromUtf8(obs_module_text("Col.Bitrate")),
		QString::fromUtf8(obs_module_text("Col.RTMP")),
		QString::fromUtf8(obs_module_text("Col.Status")),
	});
	table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(table_, 1);

	auto *row1 = new QHBoxLayout();
	btn_add_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Add")), this);
	btn_edit_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Edit")), this);
	btn_remove_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Remove")), this);
	row1->addWidget(btn_add_);
	row1->addWidget(btn_edit_);
	row1->addWidget(btn_remove_);
	layout->addLayout(row1);

	auto *row2 = new QHBoxLayout();
	btn_start_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Start")), this);
	btn_stop_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Stop")), this);
	btn_start_all_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.StartAll")), this);
	btn_stop_all_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.StopAll")), this);
	row2->addWidget(btn_start_);
	row2->addWidget(btn_stop_);
	row2->addWidget(btn_start_all_);
	row2->addWidget(btn_stop_all_);
	layout->addLayout(row2);

	connect(btn_add_, &QPushButton::clicked, this, &MultistreamDock::on_add);
	connect(btn_edit_, &QPushButton::clicked, this, &MultistreamDock::on_edit);
	connect(btn_remove_, &QPushButton::clicked, this, &MultistreamDock::on_remove);
	connect(btn_start_, &QPushButton::clicked, this, &MultistreamDock::on_start_selected);
	connect(btn_stop_, &QPushButton::clicked, this, &MultistreamDock::on_stop_selected);
	connect(btn_start_all_, &QPushButton::clicked, this, &MultistreamDock::on_start_all);
	connect(btn_stop_all_, &QPushButton::clicked, this, &MultistreamDock::on_stop_all);

	destinations_ = scenemulti::load_destinations();
	refresh_table();

	MultistreamManager::instance().set_status_callback(
		[this](const std::string &name, bool active, const std::string &error) {
			QMetaObject::invokeMethod(this,
						  [this, name, active, error]() { update_status(name, active, error); },
						  Qt::QueuedConnection);
		});
}

MultistreamDock::~MultistreamDock()
{
	MultistreamManager::instance().set_status_callback(nullptr);
	MultistreamManager::instance().stop_all();
}

int MultistreamDock::selected_row() const
{
	auto sel = table_->selectionModel()->selectedRows();
	if (sel.isEmpty())
		return -1;
	return sel.first().row();
}

void MultistreamDock::refresh_table()
{
	table_->setRowCount((int)destinations_.size());
	for (int i = 0; i < (int)destinations_.size(); i++) {
		const auto &d = destinations_[i];
		table_->setItem(i, 0, new QTableWidgetItem(d.enabled ? "✓" : ""));
		table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(d.name)));
		table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(d.scene_name)));
		table_->setItem(i, 3, new QTableWidgetItem(QString("%1x%2 @ %3").arg(d.width).arg(d.height).arg(d.fps_num / d.fps_den)));
		table_->setItem(i, 4, new QTableWidgetItem(QString("%1 kbps").arg(d.video_bitrate_kbps)));
		table_->setItem(i, 5, new QTableWidgetItem(QString::fromStdString(d.rtmp_url)));
		bool active = MultistreamManager::instance().is_active(d.name);
		table_->setItem(i, 6, new QTableWidgetItem(active ? QString::fromUtf8(obs_module_text("Status.Live"))
								   : QString::fromUtf8(obs_module_text("Status.Idle"))));
	}
}

void MultistreamDock::save()
{
	scenemulti::save_destinations(destinations_);
}

void MultistreamDock::on_add()
{
	DestinationConfig cfg;
	DestinationDialog dlg(cfg, this);
	if (dlg.exec() == QDialog::Accepted) {
		destinations_.push_back(dlg.result());
		save();
		refresh_table();
	}
}

void MultistreamDock::on_edit()
{
	int row = selected_row();
	if (row < 0 || row >= (int)destinations_.size())
		return;
	if (MultistreamManager::instance().is_active(destinations_[row].name)) {
		QMessageBox::warning(this, QString::fromUtf8(obs_module_text("Warn.Title")),
				     QString::fromUtf8(obs_module_text("Warn.StopBeforeEdit")));
		return;
	}
	DestinationDialog dlg(destinations_[row], this);
	if (dlg.exec() == QDialog::Accepted) {
		destinations_[row] = dlg.result();
		save();
		refresh_table();
	}
}

void MultistreamDock::on_remove()
{
	int row = selected_row();
	if (row < 0 || row >= (int)destinations_.size())
		return;
	if (MultistreamManager::instance().is_active(destinations_[row].name))
		MultistreamManager::instance().stop(destinations_[row].name);
	destinations_.erase(destinations_.begin() + row);
	save();
	refresh_table();
}

void MultistreamDock::start_row(int row)
{
	if (row < 0 || row >= (int)destinations_.size())
		return;
	std::string err;
	if (!MultistreamManager::instance().start(destinations_[row], err)) {
		QMessageBox::critical(this, QString::fromUtf8(obs_module_text("Err.Title")),
				      QString::fromStdString(err));
	}
	refresh_table();
}

void MultistreamDock::stop_row(int row)
{
	if (row < 0 || row >= (int)destinations_.size())
		return;
	MultistreamManager::instance().stop(destinations_[row].name);
	refresh_table();
}

void MultistreamDock::on_start_selected()
{
	start_row(selected_row());
}

void MultistreamDock::on_stop_selected()
{
	stop_row(selected_row());
}

void MultistreamDock::on_start_all()
{
	for (int i = 0; i < (int)destinations_.size(); i++) {
		if (destinations_[i].enabled && !MultistreamManager::instance().is_active(destinations_[i].name))
			start_row(i);
	}
}

void MultistreamDock::on_stop_all()
{
	MultistreamManager::instance().stop_all();
	refresh_table();
}

void MultistreamDock::update_status(const std::string &name, bool active, const std::string &error)
{
	(void)active;
	if (!error.empty()) {
		obs_log(LOG_WARNING, "[scene-multistream] '%s' stopped with error: %s", name.c_str(), error.c_str());
	}
	refresh_table();
}
