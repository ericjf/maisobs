#include "sync-dock.hpp"
#include "../oauth/providers/twitch.hpp"
#include "../plugin-support.h"

#include <obs-module.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>

SyncDock::SyncDock(QWidget *parent) : QFrame(parent)
{
	setObjectName("StreamSyncDock");

	twitch_ = new TwitchProvider(this);
	connect(twitch_, &TwitchProvider::connection_changed, this, &SyncDock::on_twitch_connection_changed);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);

	/* ── Platform connections ── */
	auto *plat_box = new QGroupBox("Platforms", this);
	auto *plat_layout = new QVBoxLayout(plat_box);

	/* Twitch row */
	{
		auto *row = new QHBoxLayout();
		btn_twitch_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.ConnectTwitch")), this);
		lbl_twitch_status_ = new QLabel(QString::fromUtf8(obs_module_text("Label.NotConnected")), this);
		chk_twitch_ = new QCheckBox("Twitch", this);
		chk_twitch_->setChecked(true);
		row->addWidget(chk_twitch_);
		row->addWidget(btn_twitch_);
		row->addWidget(lbl_twitch_status_, 1);
		plat_layout->addLayout(row);
	}

	root->addWidget(plat_box);

	/* ── Content ── */
	auto *content_box = new QGroupBox("Stream Info", this);
	auto *form = new QFormLayout(content_box);

	edit_title_ = new QLineEdit(this);
	edit_title_->setPlaceholderText("Stream title...");
	form->addRow(QString::fromUtf8(obs_module_text("Field.Title")), edit_title_);

	edit_category_ = new QLineEdit(this);
	edit_category_->setPlaceholderText("Game or category name...");
	form->addRow(QString::fromUtf8(obs_module_text("Field.Category")), edit_category_);

	root->addWidget(content_box);

	/* ── Apply ── */
	btn_apply_ = new QPushButton(QString::fromUtf8(obs_module_text("Btn.Apply")), this);
	root->addWidget(btn_apply_);

	root->addStretch();

	connect(btn_twitch_, &QPushButton::clicked, this, &SyncDock::on_connect_twitch);
	connect(btn_apply_, &QPushButton::clicked, this, &SyncDock::on_apply);

	/* Try to restore saved tokens */
	twitch_->try_restore([](bool) {});

	update_connect_button();
}

SyncDock::~SyncDock() {}

void SyncDock::on_twitch_connection_changed(bool connected)
{
	update_connect_button();
	if (connected) {
		lbl_twitch_status_->setText(QString::fromUtf8(obs_module_text("Label.ConnectedAs"))
						    .arg(twitch_->display_name()));
	} else {
		lbl_twitch_status_->setText(QString::fromUtf8(obs_module_text("Label.NotConnected")));
	}
}

void SyncDock::update_connect_button()
{
	if (twitch_->is_connected()) {
		btn_twitch_->setText(QString::fromUtf8(obs_module_text("Btn.Disconnect")));
	} else {
		btn_twitch_->setText(QString::fromUtf8(obs_module_text("Btn.ConnectTwitch")));
	}
}

void SyncDock::on_connect_twitch()
{
	if (twitch_->is_connected()) {
		twitch_->disconnect();
		return;
	}
	btn_twitch_->setEnabled(false);
	twitch_->connect([this](bool ok, const QString &error) {
		btn_twitch_->setEnabled(true);
		if (!ok) {
			QMessageBox::critical(this, QString::fromUtf8(obs_module_text("Err.Title")),
					      QString::fromUtf8(obs_module_text("Err.OAuthFailed")).arg(error));
		}
	});
}

void SyncDock::on_apply()
{
	QString title = edit_title_->text().trimmed();
	QString category = edit_category_->text().trimmed();

	if (title.isEmpty()) {
		QMessageBox::warning(this, "Warning",
				     QString::fromUtf8(obs_module_text("Warn.TitleEmpty")));
		return;
	}

	bool any = false;

	if (chk_twitch_->isChecked() && twitch_->is_connected()) {
		any = true;
		btn_apply_->setEnabled(false);
		twitch_->update_channel(title, category, [this](bool ok, const QString &err) {
			btn_apply_->setEnabled(true);
			if (!ok) {
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("Err.Title")),
						     QString::fromUtf8(obs_module_text("Err.ApplyFailed"))
							     .arg("Twitch")
							     .arg(err));
			}
		});
	}

	if (!any) {
		QMessageBox::information(this, "Info",
					 QString::fromUtf8(obs_module_text("Warn.NoConnections")));
	}
}
