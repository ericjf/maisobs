#include "oauth-settings-dialog.hpp"
#include "../oauth/platform-manager.hpp"
#include "../plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

#include <filesystem>

OAuthSettingsDialog::OAuthSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("Dialog.OAuthSettings.Title")));
	setMinimumWidth(520);

	auto *form = new QFormLayout();

	auto *twitch_header = new QLabel("<b>Twitch</b>", this);
	form->addRow(twitch_header);

	edit_twitch_id_ = new QLineEdit(this);
	edit_twitch_id_->setPlaceholderText("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
	form->addRow(QString::fromUtf8(obs_module_text("Field.TwitchClientId")), edit_twitch_id_);

	edit_twitch_secret_ = new QLineEdit(this);
	edit_twitch_secret_->setEchoMode(QLineEdit::Password);
	edit_twitch_secret_->setPlaceholderText("(leave empty for PKCE-only)");
	form->addRow(QString::fromUtf8(obs_module_text("Field.TwitchClientSecret")), edit_twitch_secret_);

	auto *link = new QLabel(QString("<a href=\"https://dev.twitch.tv/console/apps\">%1</a>")
					.arg(QString::fromUtf8(obs_module_text("Link.TwitchDevConsole"))),
				this);
	link->setOpenExternalLinks(true);
	link->setTextInteractionFlags(Qt::TextBrowserInteraction);
	form->addRow("", link);

	auto *hint = new QLabel("<i>" +
					QString::fromUtf8(
						"Register as Public Client + OAuth Redirect URL = http://localhost\n"
						"PKCE-only (no secret) is recommended.") +
					"</i>",
				this);
	hint->setWordWrap(true);
	form->addRow("", hint);

	/* Placeholder rows for v0.5 / v0.6 (disabled) */
	auto *yt_header = new QLabel("<b>YouTube</b> <span style='color:#888'>— v0.5</span>", this);
	form->addRow(yt_header);
	auto *kick_header = new QLabel("<b>Kick</b> <span style='color:#888'>— v0.6</span>", this);
	form->addRow(kick_header);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	connect(box, &QDialogButtonBox::accepted, this, &OAuthSettingsDialog::on_accept);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *root = new QVBoxLayout(this);
	root->addLayout(form);
	root->addWidget(box);

	load_from_file();
}

void OAuthSettingsDialog::load_from_file()
{
	char *path = obs_module_config_path("oauth_apps.json");
	if (path && os_file_exists(path)) {
		obs_data_t *root = obs_data_create_from_json_file(path);
		if (root) {
			obs_data_t *t = obs_data_get_obj(root, "twitch");
			if (t) {
				edit_twitch_id_->setText(obs_data_get_string(t, "client_id"));
				edit_twitch_secret_->setText(obs_data_get_string(t, "client_secret"));
				obs_data_release(t);
			}
			obs_data_release(root);
		}
	}
	bfree(path);
}

bool OAuthSettingsDialog::save_to_file()
{
	char *path = obs_module_config_path("oauth_apps.json");
	if (!path) {
		obs_log(LOG_ERROR, "[scene-multistream] obs_module_config_path returned null");
		return false;
	}

	/* Ensure parent dir exists */
	std::filesystem::path p(path);
	std::error_code ec;
	std::filesystem::create_directories(p.parent_path(), ec);

	/* Read existing JSON first to preserve other platforms (YT/Kick added later) */
	obs_data_t *root = nullptr;
	if (os_file_exists(path))
		root = obs_data_create_from_json_file(path);
	if (!root)
		root = obs_data_create();

	obs_data_t *t = obs_data_create();
	obs_data_set_string(t, "client_id", edit_twitch_id_->text().trimmed().toUtf8().constData());
	obs_data_set_string(t, "client_secret", edit_twitch_secret_->text().trimmed().toUtf8().constData());
	obs_data_set_obj(root, "twitch", t);
	obs_data_release(t);

	bool ok = obs_data_save_json(root, path);
	obs_data_release(root);
	bfree(path);

	if (!ok)
		obs_log(LOG_ERROR, "[scene-multistream] failed to save oauth_apps.json");
	return ok;
}

void OAuthSettingsDialog::on_accept()
{
	if (!save_to_file()) {
		QMessageBox::critical(this, QString::fromUtf8(obs_module_text("Err.Title")),
				      "Failed to save oauth_apps.json — check OBS log");
		return;
	}
	/* Hot-reload providers so the next Connect uses the new client_id */
	PlatformManager::instance().reload_all_credentials();
	accept();
}
