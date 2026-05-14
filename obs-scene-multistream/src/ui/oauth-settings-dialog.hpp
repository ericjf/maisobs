#pragma once

#include <QDialog>
#include <QLineEdit>

/* v0.4.1: Inline UI for setting OAuth Client IDs (Twitch v0.4, YouTube v0.5, Kick v0.6).
   Reads/writes oauth_apps.json in plugin config dir. Avoids manual JSON editing. */
class OAuthSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit OAuthSettingsDialog(QWidget *parent = nullptr);

private slots:
	void on_accept();

private:
	void load_from_file();
	bool save_to_file();

	QLineEdit *edit_twitch_id_ = nullptr;
	QLineEdit *edit_twitch_secret_ = nullptr;
};
