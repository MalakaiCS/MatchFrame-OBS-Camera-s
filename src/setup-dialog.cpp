#include "setup-dialog.hpp"

#include "controller.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace mf {

// Rows of the playback table: Replay, Highlights, Video Ads.
static int pbSlot(int k)
{
	return k == 0 ? kSlotReplay : k == 1 ? kSlotHighlights : kSlotAds;
}

SetupDialog::SetupDialog(Controller *controller, QWidget *parent) : QDialog(parent), controller_(controller)
{
	setWindowTitle("MatchFrame Multiview – Setup");
	setModal(false);
	resize(1100, 820);
	if (QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
		// Never open taller or wider than the screen; the contents scroll instead.
		const QRect avail = screen->availableGeometry();
		resize(std::min(1100, avail.width() - 40), std::min(820, avail.height() - 80));
	}
	build();
	connect(controller_, &Controller::stateChanged, this, [this]() {
		if (isVisible())
			refreshStatus();
	});
	connect(controller_, &Controller::collectionChanged, this, [this]() {
		if (isVisible())
			reload();
	});
}

void SetupDialog::build()
{
	// Everything but the Close button scrolls, so every setting stays reachable on small or
	// scaled screens.
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	auto *root = new QVBoxLayout(content);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);

	// --- Cameras
	auto *camBox = new QGroupBox(
		"Camera scenes (assign existing OBS inputs; create inputs with OBS's normal Sources workflow)", this);
	auto *grid = new QGridLayout(camBox);
	const char *headers[] = {"Slot", "Label", "Input", "Scene", "", "Framing"};
	for (int c = 0; c < 6; c++)
		grid->addWidget(new QLabel(QString("<b>%1</b>").arg(headers[c])), 0, c);
	for (int i = 0; i < 8; i++) {
		CamRow &r = cams_[i];
		r.slot = new QLabel(QString("CAM %1").arg(i + 1));
		r.label = new QLineEdit;
		r.input = new QComboBox;
		r.input->setMinimumWidth(240);
		r.scene = new QLabel;
		r.warning = new QLabel;
		r.warning->setStyleSheet("QLabel{color:#ffb74d;}");
		r.fit = new QPushButton("Fit");
		r.fill = new QPushButton("Fill");
		r.fit->setToolTip(
			"Scale the camera to fit the canvas (letterbox). Resets position/scale of this camera item.");
		r.fill->setToolTip(
			"Scale the camera to fill the canvas (crop overflow). Resets position/scale of this camera item.");
		auto *fr = new QHBoxLayout;
		fr->addWidget(r.fit);
		fr->addWidget(r.fill);
		grid->addWidget(r.slot, i + 1, 0);
		grid->addWidget(r.label, i + 1, 1);
		grid->addWidget(r.input, i + 1, 2);
		grid->addWidget(r.scene, i + 1, 3);
		grid->addWidget(r.warning, i + 1, 4);
		grid->addLayout(fr, i + 1, 5);

		connect(r.label, &QLineEdit::editingFinished, this,
			[this, i]() { controller_->setSlotLabel(i, cams_[i].label->text().trimmed()); });
		connect(r.input, &QComboBox::activated, this, [this, i](int) {
			if (loading_)
				return;
			QString msg;
			std::string uuid = cams_[i].input->currentData().toString().toStdString();
			if (!controller_->assignInput(i, uuid, &msg))
				controller_->setMessage(msg, true);
			else if (!msg.isEmpty())
				controller_->setMessage(msg, false);
			reload();
		});
		connect(r.fit, &QPushButton::clicked, this, [this, i]() {
			QString msg;
			if (!controller_->applyFraming(i, Framing::Fit, &msg))
				controller_->setMessage(msg, true);
		});
		connect(r.fill, &QPushButton::clicked, this, [this, i]() {
			QString msg;
			if (!controller_->applyFraming(i, Framing::Fill, &msg))
				controller_->setMessage(msg, true);
		});
	}
	root->addWidget(camBox);

	// --- Camera graphics
	auto *gBox = new QGroupBox("Camera graphics (one shared Browser Source referenced in all eight camera scenes)",
				   this);
	auto *gl = new QGridLayout(gBox);
	graphicsUrl_ = new QLineEdit;
	graphicsUrl_->setEchoMode(QLineEdit::Password); // URLs may carry access tokens
	graphicsUrl_->setPlaceholderText("Existing MatchFrame graphics output URL (not the marketing homepage)");
	showUrl_ = new QPushButton("Show");
	showUrl_->setCheckable(true);
	applyUrl_ = new QPushButton("Apply URL");
	auto *reloadNote = new QLabel("⚠ Apply URL reloads the graphics page (clock/animations restart). "
				      "Camera cuts never reload it.");
	reloadNote->setStyleSheet("QLabel{color:#ffb74d;}");
	graphicsState_ = new QLabel;
	canvas_ = new QLabel;
	gl->addWidget(new QLabel("Graphics output URL:"), 0, 0);
	gl->addWidget(graphicsUrl_, 0, 1);
	gl->addWidget(showUrl_, 0, 2);
	gl->addWidget(applyUrl_, 0, 3);
	gl->addWidget(reloadNote, 1, 1, 1, 3);
	gl->addWidget(graphicsState_, 2, 1, 1, 3);
	gl->addWidget(canvas_, 3, 1, 1, 3);
	programOnly_ =
		new QCheckBox("Show camera graphics on Program only (multiview camera tiles and Preview stay clean)");
	programOnly_->setToolTip(
		"Program, recording and stream keep the overlay. The multiview's camera tiles and its "
		"Preview pane draw the cameras without it, so you can see every camera during a break.");
	gl->addWidget(programOnly_, 4, 1, 1, 3);
	connect(programOnly_, &QCheckBox::toggled, this, [this](bool on) {
		controller_->setGraphicsProgramOnly(on);
		refreshStatus();
	});
	connect(showUrl_, &QPushButton::toggled, this,
		[this](bool on) { graphicsUrl_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	connect(applyUrl_, &QPushButton::clicked, this, [this]() {
		QString msg;
		controller_->applyGraphicsUrl(graphicsUrl_->text(), &msg);
		controller_->setMessage(msg, false);
		refreshStatus();
	});
	root->addWidget(gBox);

	// --- Playback
	auto *pBox = new QGroupBox("Playback scenes (prepared files; independent of camera graphics). Video Ads plays "
				   "its ads in file-name order, once.",
				   this);
	auto *pl = new QGridLayout(pBox);
	const char *pbHeaders[] = {"Scene", "Label", "File", "", "State", "Overlay URL (optional)", "", ""};
	for (int c = 0; c < 8; c++)
		pl->addWidget(new QLabel(QString("<b>%1</b>").arg(pbHeaders[c])), 0, c);
	for (int k = 0; k < 3; k++) {
		const int slot = pbSlot(k);
		PbRow &r = pbs_[k];
		r.scene = new QLabel;
		r.label = new QLineEdit;
		r.file = new QLabel;
		r.file->setMinimumWidth(200);
		r.browse = new QPushButton(slot == kSlotAds ? "Load Ads…" : "Load / Arm…");
		r.state = new QLabel;
		r.overlayUrl = new QLineEdit;
		r.overlayUrl->setEchoMode(QLineEdit::Password);
		r.overlayUrl->setPlaceholderText(slot == kSlotReplay ? "e.g. REPLAY branding page"
						 : slot == kSlotAds  ? "e.g. sponsor strap over the ads"
								     : "Highlights branding page");
		r.overlayOn = new QCheckBox("Show");
		r.overlayApply = new QPushButton("Apply");
		r.overlayApply->setToolTip("Applying a changed URL reloads that overlay page");
		pl->addWidget(r.scene, k + 1, 0);
		pl->addWidget(r.label, k + 1, 1);
		pl->addWidget(r.file, k + 1, 2);
		pl->addWidget(r.browse, k + 1, 3);
		pl->addWidget(r.state, k + 1, 4);
		pl->addWidget(r.overlayUrl, k + 1, 5);
		pl->addWidget(r.overlayOn, k + 1, 6);
		pl->addWidget(r.overlayApply, k + 1, 7);
		connect(r.label, &QLineEdit::editingFinished, this,
			[this, k, slot]() { controller_->setSlotLabel(slot, pbs_[k].label->text().trimmed()); });
		connect(r.browse, &QPushButton::clicked, this, [this, slot]() {
			PlaybackView v = controller_->playback().view(slot);
			if (!v.canModify) {
				controller_->setMessage(QString::fromStdString(v.modifyBlockReason), true);
				return;
			}
			const QString filter =
				"Media files (*.mp4 *.mov *.mkv *.m4v *.ts *.webm *.avi *.mxf *.flv);;All files (*)";
			if (slot == kSlotAds) {
				QStringList files = QFileDialog::getOpenFileNames(
					this, "Choose video ads (they play in file-name order)", QString(), filter);
				std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
					return QString::localeAwareCompare(QFileInfo(a).fileName(),
									   QFileInfo(b).fileName()) < 0;
				});
				if (!files.isEmpty())
					controller_->loadAdsPlaylist(files);
			} else {
				QString path =
					QFileDialog::getOpenFileName(this, "Choose prepared media", QString(), filter);
				if (!path.isEmpty())
					controller_->loadPlayback(slot, path);
			}
			refreshStatus();
		});
		connect(r.overlayApply, &QPushButton::clicked, this, [this, k, slot]() {
			QString msg;
			controller_->setPlaybackOverlay(slot, pbs_[k].overlayOn->isChecked(),
							pbs_[k].overlayUrl->text(), &msg);
			if (!msg.isEmpty())
				controller_->setMessage(msg, false);
		});
		connect(r.overlayOn, &QCheckBox::toggled, this, [this, k, slot](bool on) {
			if (loading_)
				return;
			QString msg;
			controller_->setPlaybackOverlay(
				slot, on, QString::fromStdString(controller_->config().playback(slot).graphicsUrl),
				&msg);
		});
	}
	root->addWidget(pBox);

	// --- Intermission
	auto *iBox = new QGroupBox("Intermission (a graphics page shown full screen; no camera graphics)", this);
	auto *il = new QGridLayout(iBox);
	intermissionScene_ = new QLabel;
	intermissionLabel_ = new QLineEdit;
	intermissionUrl_ = new QLineEdit;
	intermissionUrl_->setEchoMode(QLineEdit::Password); // URLs may carry access tokens
	intermissionUrl_->setPlaceholderText("Intermission / break page URL (e.g. a MatchFrame break screen)");
	auto *showIm = new QPushButton("Show");
	showIm->setCheckable(true);
	intermissionApply_ = new QPushButton("Apply");
	intermissionApply_->setToolTip("Applying a changed URL reloads the intermission page");
	il->addWidget(new QLabel("<b>Scene</b>"), 0, 0);
	il->addWidget(new QLabel("<b>Label</b>"), 0, 1);
	il->addWidget(new QLabel("<b>Page URL</b>"), 0, 2);
	il->addWidget(intermissionScene_, 1, 0);
	il->addWidget(intermissionLabel_, 1, 1);
	il->addWidget(intermissionUrl_, 1, 2);
	il->addWidget(showIm, 1, 3);
	il->addWidget(intermissionApply_, 1, 4);
	il->setColumnStretch(2, 1);
	connect(showIm, &QPushButton::toggled, this,
		[this](bool on) { intermissionUrl_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	connect(intermissionLabel_, &QLineEdit::editingFinished, this,
		[this]() { controller_->setSlotLabel(kSlotIntermission, intermissionLabel_->text().trimmed()); });
	connect(intermissionApply_, &QPushButton::clicked, this, [this]() {
		QString msg;
		controller_->applyIntermissionUrl(intermissionUrl_->text(), &msg);
		controller_->setMessage(msg, false);
		refreshStatus();
	});
	root->addWidget(iBox);

	// --- Replay / Highlights provider
	auto *rcBox = new QGroupBox("Replay / Highlights source", this);
	auto *rl = new QGridLayout(rcBox);
	provider_ = new QComboBox;
	provider_->addItem("Built-in: prepared files (MF — REPLAY / MF — HIGHLIGHTS)", false);
	provider_->addItem("MatchFrame Replay Cue (multi-angle instant replay plugin)", true);
	rcState_ = new QLabel;
	rcState_->setWordWrap(true);
	rcReplayScene_ = new QComboBox;
	rcHighlightsScene_ = new QComboBox;
	rl->addWidget(new QLabel("Source:"), 0, 0);
	rl->addWidget(provider_, 0, 1, 1, 3);
	rl->addWidget(rcState_, 1, 1, 1, 3);
	rl->addWidget(new QLabel("Replay Cue Replay scene:"), 2, 0);
	rl->addWidget(rcReplayScene_, 2, 1);
	rl->addWidget(new QLabel("Highlights scene:"), 2, 2);
	rl->addWidget(rcHighlightsScene_, 2, 3);
	auto *rcNote = new QLabel(
		"With Replay Cue, the Replay and Highlights tiles show Replay Cue's scenes. 9 cues the selected replay "
		"into Preview and CUT plays it with Replay Cue's own transition; 0 stages the break's highlights reel. "
		"Requires Replay Cue (OBS 32.2+) and obs-websocket (built into OBS).");
	rcNote->setWordWrap(true);
	rcNote->setStyleSheet("QLabel{color:#9e9e9e;}");
	rl->addWidget(rcNote, 3, 0, 1, 4);
	connect(provider_, &QComboBox::activated, this, [this](int) {
		if (loading_)
			return;
		controller_->setPlaybackProvider(provider_->currentData().toBool());
		refreshStatus();
	});
	auto sceneChanged = [this](int) {
		if (loading_)
			return;
		controller_->setReplayCueScenes(rcReplayScene_->currentData().toString().toStdString(),
						rcHighlightsScene_->currentData().toString().toStdString());
	};
	connect(rcReplayScene_, &QComboBox::activated, this, sceneChanged);
	connect(rcHighlightsScene_, &QComboBox::activated, this, sceneChanged);
	root->addWidget(rcBox);

	// --- Repair
	auto *rBox = new QHBoxLayout;
	repair_ = new QPushButton("Create / Repair MatchFrame Scenes");
	repair_->setMinimumHeight(34);
	repairNote_ = new QLabel;
	repairNote_->setWordWrap(true);
	rBox->addWidget(repair_);
	rBox->addWidget(repairNote_, 1);
	root->addLayout(rBox);
	connect(repair_, &QPushButton::clicked, this, &SetupDialog::repair);

	log_ = new QPlainTextEdit;
	log_->setReadOnly(true);
	log_->setMaximumHeight(150);
	root->addWidget(log_);

	auto *close = new QPushButton("Close");
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	auto *cl = new QHBoxLayout;
	cl->addStretch(1);
	cl->addWidget(close);
	outer->addLayout(cl);
}

void SetupDialog::reload()
{
	loading_ = true;
	BankConfig &cfg = controller_->config();
	auto inputs = controller_->bank().videoInputs();
	for (int i = 0; i < 8; i++) {
		CamRow &r = cams_[i];
		r.label->setText(QString::fromStdString(cfg.cameras[i].label));
		r.input->clear();
		r.input->addItem("— Unassigned —", QString());
		int sel = 0;
		for (const auto &in : inputs) {
			r.input->addItem(QString::fromStdString(in.name), QString::fromStdString(in.uuid));
			if (in.uuid == cfg.cameras[i].inputUuid)
				sel = r.input->count() - 1;
		}
		if (sel == 0 && !cfg.cameras[i].inputUuid.empty()) {
			r.input->addItem(QString("⚠ missing: %1").arg(QString::fromStdString(cfg.cameras[i].inputName)),
					 QString::fromStdString(cfg.cameras[i].inputUuid));
			sel = r.input->count() - 1;
		}
		r.input->setCurrentIndex(sel);
	}
	graphicsUrl_->setText(QString::fromStdString(cfg.graphicsUrl));
	intermissionLabel_->setText(QString::fromStdString(cfg.intermission.label));
	intermissionUrl_->setText(QString::fromStdString(cfg.intermission.graphicsUrl));
	{
		QSignalBlocker block(programOnly_);
		programOnly_->setChecked(cfg.graphicsProgramOnly);
	}
	for (int k = 0; k < 3; k++) {
		const int slot = pbSlot(k);
		const PlaybackSlotConfig &p = cfg.playback(slot);
		pbs_[k].label->setText(QString::fromStdString(p.label));
		pbs_[k].overlayUrl->setText(QString::fromStdString(p.graphicsUrl));
		pbs_[k].overlayOn->setChecked(p.graphicsEnabled);
	}
	provider_->setCurrentIndex(controller_->replayCueMode() ? 1 : 0);
	reloadReplayCueScenes();
	loading_ = false;
	refreshStatus();
}

void SetupDialog::reloadReplayCueScenes()
{
	const BankConfig &cfg = controller_->config();
	auto fill = [](QComboBox *box, const std::string &current, const QString &autoText) {
		box->clear();
		box->addItem(autoText, QString());
		for (const auto &c : ReplayCueLink::sceneCandidates())
			box->addItem(QString::fromStdString(c.name) + (c.hasPlayer ? "  (Replay Cue player)" : ""),
				     QString::fromStdString(c.name));
		int i = box->findData(QString::fromStdString(current));
		box->setCurrentIndex(i >= 0 ? i : 0);
	};
	fill(rcReplayScene_, cfg.replayCueReplayScene, "Auto (Replay Cue's \"Replay\" scene)");
	fill(rcHighlightsScene_, cfg.replayCueHighlightsScene, "Auto (Replay Cue's highlights scene)");
}

void SetupDialog::refreshStatus()
{
	BankConfig &cfg = controller_->config();
	SceneBank &bank = controller_->bank();
	for (int i = 0; i < 8; i++) {
		CamRow &r = cams_[i];
		OBSSourceAutoRelease scene = bank.slotScene(i);
		r.scene->setText(scene ? QString::fromStdString(nameOf(scene)) : "<i>not created</i>");
		QStringList w;
		auto shared = bank.slotsSharingInput(i);
		if (!shared.empty()) {
			QStringList n;
			for (int s : shared)
				n << QString::number(s + 1);
			w << QString("same input as CAM %1 (same feed, not an extra camera)").arg(n.join(", "));
		}
		if (!cfg.cameras[i].inputUuid.empty()) {
			OBSSourceAutoRelease in = bank.cameraInput(i);
			if (!in)
				w << "assigned input missing";
		}
		r.warning->setText(w.join("; "));
		bool hasItem = scene && bank.cameraInput(i);
		r.fit->setEnabled(hasItem);
		r.fill->setEnabled(hasItem);
	}

	uint32_t w, h;
	canvasSize(w, h);
	obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	canvas_->setText(QString("OBS canvas: %1×%2, output %3×%4 @ %5 fps (the graphics page is sized to the canvas)")
				 .arg(w)
				 .arg(h)
				 .arg(ovi.output_width)
				 .arg(ovi.output_height)
				 .arg(ovi.fps_den ? (double)ovi.fps_num / ovi.fps_den : 0.0, 0, 'f', 2));
	GraphicsVisibility gv = controller_->graphics().visibility();
	OBSSourceAutoRelease browser = bank.cameraGraphics();
	graphicsState_->setText(QString("Camera graphics: %1 · source: %2 · configured URL: %3")
					.arg(graphicsVisibilityName(gv),
					     browser ? QString::fromStdString(nameOf(browser)) : QString("not created"),
					     QString::fromStdString(redactUrl(cfg.graphicsUrl)).toHtmlEscaped()));
	applyUrl_->setEnabled(controller_->collectionReady());

	for (int k = 0; k < 3; k++) {
		const int slot = pbSlot(k);
		PlaybackView v = controller_->playback().view(slot);
		OBSSourceAutoRelease scene = bank.slotScene(slot);
		pbs_[k].scene->setText(scene ? QString::fromStdString(nameOf(scene)) : "<i>not created</i>");
		QString fileText = v.fileName.empty() ? "<i>no file</i>"
						      : QString::fromStdString(v.fileName).toHtmlEscaped();
		if (v.playlistCount > 0)
			fileText = QString("Ad %1/%2 · %3").arg(v.playlistIndex + 1).arg(v.playlistCount).arg(fileText);
		pbs_[k].file->setText(fileText);
		pbs_[k].file->setToolTip(QString::fromStdString(v.file));
		QString st = playbackStateName(v.state);
		if (!v.error.empty())
			st += " – " + QString::fromStdString(v.error);
		if (v.onProgram)
			st += " (ON PROGRAM)";
		pbs_[k].state->setText(st);
		pbs_[k].browse->setEnabled(v.sourceExists && v.canModify);
		pbs_[k].browse->setToolTip(v.canModify ? "" : QString::fromStdString(v.modifyBlockReason));
		bool browserOk = controller_->graphics().browserSupported();
		pbs_[k].overlayUrl->setEnabled(browserOk);
		pbs_[k].overlayOn->setEnabled(browserOk);
		pbs_[k].overlayApply->setEnabled(browserOk);
	}

	{
		OBSSourceAutoRelease scene = bank.slotScene(kSlotIntermission);
		intermissionScene_->setText(scene ? QString::fromStdString(nameOf(scene)) : "<i>not created</i>");
		intermissionApply_->setEnabled(controller_->graphics().browserSupported() &&
					       controller_->collectionReady());
	}

	QString why;
	bool allowed = controller_->structuralAllowed(&why);
	repair_->setEnabled(allowed);
	for (auto &r : cams_)
		r.input->setEnabled(allowed);
	repairNote_->setText(allowed ? "Idempotent: reuses plugin-owned scenes and sources, preserves manual framing, "
				       "filters and unrelated sources, and never switches Program or starts outputs."
				     : why);

	const bool rc = controller_->replayCueMode();
	rcReplayScene_->setEnabled(rc);
	rcHighlightsScene_->setEnabled(rc);
	for (auto &p : pbs_)
		p.browse->setEnabled(p.browse->isEnabled() && !rc); // files are Replay Cue's in that mode
	controller_->replayCue().refresh(true);
	const ReplayCueStatus &st = controller_->replayCue().status();
	const auto names = controller_->replayCueScenes();
	if (!st.ok) {
		rcState_->setText(QString("Replay Cue: not detected (%1)").arg(QString::fromStdString(st.error)));
		rcState_->setStyleSheet("QLabel{color:#ff8a65;}");
	} else {
		rcState_->setText(
			QString("Replay Cue: connected · %1 · %2 replays · Replay scene: %3 · Highlights scene: %4")
				.arg(st.armed ? "armed" : "not armed")
				.arg(st.eventCount)
				.arg(names.first.empty() ? QString("not created yet")
							 : QString::fromStdString(names.first))
				.arg(names.second.empty() ? QString("not created yet")
							  : QString::fromStdString(names.second)));
		rcState_->setStyleSheet("QLabel{color:#9ccc65;}");
	}
}

void SetupDialog::repair()
{
	RepairReport r = controller_->createOrRepair();
	QStringList lines;
	for (auto &l : r.lines)
		lines << QString::fromStdString(l);
	lines << QString("Done: %1 created, %2 reused, %3 warning(s).").arg(r.created).arg(r.reused).arg(r.warnings);
	log_->setPlainText(lines.join("\n"));
	reload();
}

} // namespace mf
