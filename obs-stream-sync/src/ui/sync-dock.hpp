#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>

class TwitchProvider;

class SyncDock : public QFrame {
	Q_OBJECT
public:
	explicit SyncDock(QWidget *parent = nullptr);
	~SyncDock() override;

private slots:
	void on_connect_twitch();
	void on_apply();
	void on_twitch_connection_changed(bool connected);

private:
	void update_connect_button();

	TwitchProvider *twitch_ = nullptr;

	/* Twitch row */
	QPushButton *btn_twitch_ = nullptr;
	QLabel *lbl_twitch_status_ = nullptr;

	/* Content */
	QLineEdit *edit_title_ = nullptr;
	QLineEdit *edit_category_ = nullptr;
	QCheckBox *chk_twitch_ = nullptr;

	/* Apply */
	QPushButton *btn_apply_ = nullptr;
};
