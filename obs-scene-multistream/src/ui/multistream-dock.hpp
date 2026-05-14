#pragma once

#include "../destination.hpp"

#include <QFrame>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>

#include <vector>

class MultistreamDock : public QFrame {
	Q_OBJECT

public:
	explicit MultistreamDock(QWidget *parent = nullptr);
	~MultistreamDock() override;

private slots:
	void on_add();
	void on_edit();
	void on_remove();
	void on_start_selected();
	void on_stop_selected();
	void on_start_all();
	void on_stop_all();

private:
	void refresh_table();
	void save();
	void update_status(const std::string &name, bool active, const std::string &error);

	int selected_row() const;
	void start_row(int row);
	void stop_row(int row);

	std::vector<DestinationConfig> destinations_;

	QTableWidget *table_ = nullptr;
	QPushButton *btn_add_ = nullptr;
	QPushButton *btn_edit_ = nullptr;
	QPushButton *btn_remove_ = nullptr;
	QPushButton *btn_start_ = nullptr;
	QPushButton *btn_stop_ = nullptr;
	QPushButton *btn_start_all_ = nullptr;
	QPushButton *btn_stop_all_ = nullptr;

	/* v0.4: OAuth topbar */
	QPushButton *btn_twitch_ = nullptr;
	QLabel *lbl_twitch_status_ = nullptr;
	/* v0.4.1: OAuth Settings dialog launcher */
	QPushButton *btn_oauth_settings_ = nullptr;

	void update_twitch_button();
	void open_oauth_settings();
};
