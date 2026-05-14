#include "destination-dialog.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>

DestinationDialog::DestinationDialog(const DestinationConfig &cfg, QWidget *parent) : QDialog(parent), initial_(cfg)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("Dialog.Title")));
	setMinimumWidth(480);

	auto *form = new QFormLayout();

	edit_name_ = new QLineEdit(QString::fromStdString(cfg.name), this);
	form->addRow(QString::fromUtf8(obs_module_text("Field.Name")), edit_name_);

	combo_scene_ = new QComboBox(this);
	populate_scenes();
	if (cfg.follow_obs_scene) {
		combo_scene_->setCurrentIndex(0); /* "(Follow OBS)" — index 0 */
	} else {
		int idx = combo_scene_->findText(QString::fromStdString(cfg.scene_name));
		if (idx >= 0)
			combo_scene_->setCurrentIndex(idx);
	}
	form->addRow(QString::fromUtf8(obs_module_text("Field.Scene")), combo_scene_);

	check_use_obs_output_ = new QCheckBox(QString::fromUtf8(obs_module_text("Field.UseOBSOutput")), this);
	check_use_obs_output_->setChecked(cfg.follow_obs_video);
	form->addRow("", check_use_obs_output_);

	edit_url_ = new QLineEdit(QString::fromStdString(cfg.rtmp_url), this);
	edit_url_->setPlaceholderText("rtmp://... or rtmps://...");
	form->addRow(QString::fromUtf8(obs_module_text("Field.RTMP")), edit_url_);

	edit_key_ = new QLineEdit(QString::fromStdString(cfg.stream_key), this);
	edit_key_->setEchoMode(QLineEdit::Password);
	form->addRow(QString::fromUtf8(obs_module_text("Field.Key")), edit_key_);

	combo_resolution_ = new QComboBox(this);
	combo_resolution_->setEditable(true);
	populate_common_resolutions();
	combo_resolution_->setCurrentText(QString("%1x%2").arg(cfg.width).arg(cfg.height));
	form->addRow(QString::fromUtf8(obs_module_text("Field.Resolution")), combo_resolution_);

	spin_fps_ = new QSpinBox(this);
	spin_fps_->setRange(1, 240);
	spin_fps_->setValue((int)(cfg.fps_num / cfg.fps_den));
	form->addRow(QString::fromUtf8(obs_module_text("Field.FPS")), spin_fps_);

	combo_video_encoder_ = new QComboBox(this);
	populate_video_encoders();
	int vei = combo_video_encoder_->findData(QString::fromStdString(cfg.video_encoder));
	if (vei >= 0)
		combo_video_encoder_->setCurrentIndex(vei);
	form->addRow(QString::fromUtf8(obs_module_text("Field.VideoEncoder")), combo_video_encoder_);

	spin_video_bitrate_ = new QSpinBox(this);
	spin_video_bitrate_->setRange(100, 100000);
	spin_video_bitrate_->setSuffix(" kbps");
	spin_video_bitrate_->setValue(cfg.video_bitrate_kbps);
	form->addRow(QString::fromUtf8(obs_module_text("Field.VideoBitrate")), spin_video_bitrate_);

	combo_audio_encoder_ = new QComboBox(this);
	populate_audio_encoders();
	int aei = combo_audio_encoder_->findData(QString::fromStdString(cfg.audio_encoder));
	if (aei >= 0)
		combo_audio_encoder_->setCurrentIndex(aei);
	form->addRow(QString::fromUtf8(obs_module_text("Field.AudioEncoder")), combo_audio_encoder_);

	spin_audio_bitrate_ = new QSpinBox(this);
	spin_audio_bitrate_->setRange(32, 512);
	spin_audio_bitrate_->setSuffix(" kbps");
	spin_audio_bitrate_->setValue(cfg.audio_bitrate_kbps);
	form->addRow(QString::fromUtf8(obs_module_text("Field.AudioBitrate")), spin_audio_bitrate_);

	spin_audio_track_ = new QSpinBox(this);
	spin_audio_track_->setRange(0, 5);
	spin_audio_track_->setValue((int)cfg.audio_track);
	form->addRow(QString::fromUtf8(obs_module_text("Field.AudioTrack")), spin_audio_track_);

	check_enabled_ = new QCheckBox(this);
	check_enabled_->setChecked(cfg.enabled);
	form->addRow(QString::fromUtf8(obs_module_text("Field.Enabled")), check_enabled_);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(box, &QDialogButtonBox::accepted, this, &DestinationDialog::on_accept);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *root = new QVBoxLayout(this);
	root->addLayout(form);
	root->addWidget(box);
}

void DestinationDialog::populate_scenes()
{
	combo_scene_->clear();
	/* v0.3: "(Follow OBS)" is always the first option (sentinel: empty userData) */
	combo_scene_->addItem(QString::fromUtf8(obs_module_text("Scene.FollowOBS")), QVariant(QString()));
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *s = scenes.sources.array[i];
		const char *n = obs_source_get_name(s);
		if (n)
			combo_scene_->addItem(QString::fromUtf8(n), QVariant(QString::fromUtf8(n)));
	}
	obs_frontend_source_list_free(&scenes);
}

void DestinationDialog::populate_video_encoders()
{
	combo_video_encoder_->clear();
	const char *id;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		const char *name = obs_encoder_get_display_name(id);
		combo_video_encoder_->addItem(QString::fromUtf8(name ? name : id), QString::fromUtf8(id));
	}
}

void DestinationDialog::populate_audio_encoders()
{
	combo_audio_encoder_->clear();
	const char *id;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_AUDIO)
			continue;
		const char *name = obs_encoder_get_display_name(id);
		combo_audio_encoder_->addItem(QString::fromUtf8(name ? name : id), QString::fromUtf8(id));
	}
}

void DestinationDialog::populate_common_resolutions()
{
	combo_resolution_->addItem("1920x1080");
	combo_resolution_->addItem("1280x720");
	combo_resolution_->addItem("1080x1920");
	combo_resolution_->addItem("720x1280");
	combo_resolution_->addItem("3840x2160");
	combo_resolution_->addItem("2560x1440");
}

void DestinationDialog::on_accept()
{
	result_ = initial_;
	result_.name = edit_name_->text().trimmed().toStdString();
	/* v0.3: index 0 = "(Follow OBS)" — sentinel via empty userData QVariant */
	QString scene_data = combo_scene_->currentData().toString();
	result_.follow_obs_scene = scene_data.isEmpty();
	result_.scene_name = result_.follow_obs_scene ? std::string() : scene_data.toStdString();
	result_.follow_obs_video = check_use_obs_output_->isChecked();
	result_.rtmp_url = edit_url_->text().trimmed().toStdString();
	result_.stream_key = edit_key_->text().toStdString();
	result_.video_encoder = combo_video_encoder_->currentData().toString().toStdString();
	result_.audio_encoder = combo_audio_encoder_->currentData().toString().toStdString();

	QString res = combo_resolution_->currentText();
	int x = res.indexOf('x');
	if (x > 0) {
		result_.width = res.left(x).toUInt();
		result_.height = res.mid(x + 1).toUInt();
	}
	result_.fps_num = (uint32_t)spin_fps_->value();
	result_.fps_den = 1;
	result_.video_bitrate_kbps = spin_video_bitrate_->value();
	result_.audio_bitrate_kbps = spin_audio_bitrate_->value();
	result_.audio_track = (uint32_t)spin_audio_track_->value();
	result_.enabled = check_enabled_->isChecked();

	if (result_.name.empty()) {
		QMessageBox::warning(this, "", QString::fromUtf8(obs_module_text("Err.NameEmpty")));
		return;
	}
	/* v0.3: scene_name may be empty when follow_obs_scene=true */
	if (!result_.follow_obs_scene && result_.scene_name.empty()) {
		QMessageBox::warning(this, "", QString::fromUtf8(obs_module_text("Err.SceneEmpty")));
		return;
	}
	if (result_.rtmp_url.empty()) {
		QMessageBox::warning(this, "", QString::fromUtf8(obs_module_text("Err.URLEmpty")));
		return;
	}
	if (result_.width == 0 || result_.height == 0) {
		QMessageBox::warning(this, "", QString::fromUtf8(obs_module_text("Err.BadResolution")));
		return;
	}

	accept();
}
