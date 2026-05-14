#pragma once

#include "../destination.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>

class DestinationDialog : public QDialog {
	Q_OBJECT

public:
	explicit DestinationDialog(const DestinationConfig &cfg, QWidget *parent = nullptr);
	DestinationConfig result() const { return result_; }

private slots:
	void on_accept();

private:
	void populate_scenes();
	void populate_video_encoders();
	void populate_audio_encoders();
	void populate_common_resolutions();

	DestinationConfig initial_;
	DestinationConfig result_;

	QLineEdit *edit_name_ = nullptr;
	QComboBox *combo_scene_ = nullptr;
	QCheckBox *check_use_obs_output_ = nullptr; /* v0.3 */
	QLineEdit *edit_url_ = nullptr;
	QLineEdit *edit_key_ = nullptr;
	QComboBox *combo_video_encoder_ = nullptr;
	QComboBox *combo_audio_encoder_ = nullptr;
	QComboBox *combo_resolution_ = nullptr;
	QSpinBox *spin_fps_ = nullptr;
	QSpinBox *spin_video_bitrate_ = nullptr;
	QSpinBox *spin_audio_bitrate_ = nullptr;
	QSpinBox *spin_audio_track_ = nullptr;
	QCheckBox *check_enabled_ = nullptr;
};
