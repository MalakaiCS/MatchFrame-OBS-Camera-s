#include "multiview-window.hpp"

#include "controller.hpp"
#include "setup-dialog.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QVBoxLayout>

#include <functional>

namespace mf {

static QPushButton *makeButton(const QString &text, QWidget *parent)
{
	auto *b = new QPushButton(text, parent);
	// Buttons never take keyboard focus, so Space always means CUT in this window.
	b->setFocusPolicy(Qt::NoFocus);
	b->setMinimumHeight(34);
	return b;
}

MultiviewWindow::MultiviewWindow(Controller *controller, QWidget *parent)
	: QWidget(parent, Qt::Window),
	  controller_(controller)
{
	setWindowTitle("MatchFrame Multiview");
	setMinimumSize(960, 640);
	setFocusPolicy(Qt::StrongFocus);
	buildUi();
	restoreGeometryPrefs();
	connect(controller_, &Controller::stateChanged, this, &MultiviewWindow::refresh);
	connect(controller_, &Controller::messageChanged, this, &MultiviewWindow::onMessage);
	connect(controller_, &Controller::collectionChanged, this, [this]() {
		releaseRendering();
		refresh();
	});
}

MultiviewWindow::~MultiviewWindow()
{
	if (setupDialog_)
		delete setupDialog_.data();
	releaseRendering();
}

void MultiviewWindow::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	// Toolbar
	auto *bar = new QHBoxLayout();
	cut_ = makeButton("CUT  [Space]", this);
	cut_->setStyleSheet("QPushButton{font-weight:bold;background:#7a1010;color:white;padding:4px 18px;}"
			    "QPushButton:disabled{background:#3a2a2a;color:#888;}");
	returnLive_ = makeButton("RETURN LIVE", this);
	returnLive_->setToolTip("Immediate on-air cut to the last camera that was on Program");
	graphics_ = makeButton("Camera Graphics", this);
	setup_ = makeButton("Setup…", this);
	fullScreen_ = makeButton("Full Screen", this);
	studio_ = makeButton("Enable Studio Mode", this);
	studio_->setStyleSheet("QPushButton{background:#8a6d00;color:white;}");
	transition_ = new QLabel(this);
	streaming_ = new QLabel(this);
	recording_ = new QLabel(this);
	state_ = new QLabel(this);
	for (QLabel *l : {streaming_, recording_})
		l->setMargin(4);

	bar->addWidget(cut_);
	bar->addWidget(returnLive_);
	bar->addWidget(graphics_);
	bar->addWidget(studio_);
	bar->addSpacing(12);
	bar->addWidget(transition_);
	bar->addWidget(state_);
	bar->addStretch(1);
	bar->addWidget(streaming_);
	bar->addWidget(recording_);
	bar->addSpacing(12);
	bar->addWidget(setup_);
	bar->addWidget(fullScreen_);
	root->addLayout(bar);

	warning_ = new QLabel(this);
	warning_->setStyleSheet("QLabel{background:#5a4300;color:white;padding:3px;}");
	warning_->setWordWrap(true);
	warning_->hide();
	root->addWidget(warning_);

	surface_ = new RenderSurface(this);
	root->addWidget(surface_, 1);

	builtinPanels_ = new QWidget(this);
	auto *pbRow = new QHBoxLayout(builtinPanels_);
	pbRow->setContentsMargins(0, 0, 0, 0);
	pbRow->addWidget(buildPlaybackPanel(kSlotReplay, pb_[0]), 1);
	pbRow->addWidget(buildPlaybackPanel(kSlotHighlights, pb_[1]), 1);
	root->addWidget(builtinPanels_);
	rcPanel_ = buildReplayCuePanel();
	rcPanel_->hide();
	root->addWidget(rcPanel_);

	message_ = new QLabel(this);
	message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	root->addWidget(message_);

	connect(cut_, &QPushButton::clicked, controller_, &Controller::cut);
	connect(returnLive_, &QPushButton::clicked, controller_, &Controller::returnLive);
	connect(graphics_, &QPushButton::clicked, controller_, &Controller::toggleGraphics);
	connect(studio_, &QPushButton::clicked, controller_, &Controller::enableStudioMode);
	connect(setup_, &QPushButton::clicked, this, &MultiviewWindow::openSetup);
	connect(fullScreen_, &QPushButton::clicked, this, &MultiviewWindow::toggleFullScreen);
	connect(surface_, &RenderSurface::paneClicked, this, &MultiviewWindow::onPaneClicked);
	connect(surface_, &RenderSurface::paneDoubleClicked, this, &MultiviewWindow::onPaneDoubleClicked);
}

QWidget *MultiviewWindow::buildPlaybackPanel(int slot, PlaybackUi &ui)
{
	auto *box = new QWidget(this);
	box->setObjectName("pbPanel");
	box->setStyleSheet("#pbPanel{border:1px solid #444;border-radius:3px;}");
	// Two rows (info, then transport) so buttons never truncate at the minimum width.
	auto *l = new QGridLayout(box);
	l->setContentsMargins(6, 4, 6, 4);
	l->setVerticalSpacing(3);
	ui.title = new QLabel(slot == kSlotReplay ? "<b>REPLAY</b> [9]" : "<b>HIGHLIGHTS</b> [0]", box);
	ui.file = new QLabel(box);
	ui.file->setMinimumWidth(120);
	ui.status = new QLabel(box);
	ui.status->setMinimumWidth(80);
	ui.time = new QLabel(box);
	ui.time->setMinimumWidth(90);
	ui.load = makeButton("Load / Arm…", box);
	ui.arm = makeButton("Arm", box);
	ui.arm->setToolTip("Re-arm the loaded file at its first frame (off-air only)");
	ui.play = makeButton("Play", box);
	ui.restart = makeButton("Restart", box);
	auto *info = new QHBoxLayout;
	info->addWidget(ui.title);
	info->addWidget(ui.file, 1);
	info->addWidget(ui.status);
	info->addWidget(ui.time);
	auto *buttons = new QHBoxLayout;
	for (QPushButton *b : {ui.load, ui.arm, ui.play, ui.restart})
		buttons->addWidget(b, 1);
	l->addLayout(info, 0, 0);
	l->addLayout(buttons, 1, 0);
	ui.file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	ui.play->setMinimumWidth(110);
	ui.restart->setMinimumWidth(110);
	connect(ui.load, &QPushButton::clicked, this, [this, slot]() { chooseFile(slot); });
	connect(ui.arm, &QPushButton::clicked, this, [this, slot]() { controller_->armPlayback(slot); });
	connect(ui.play, &QPushButton::clicked, this, [this, slot]() { controller_->playPausePlayback(slot); });
	connect(ui.restart, &QPushButton::clicked, this, [this, slot]() { controller_->restartPlayback(slot); });
	return box;
}

QWidget *MultiviewWindow::buildReplayCuePanel()
{
	auto *box = new QWidget(this);
	box->setObjectName("rcPanel");
	box->setStyleSheet("#rcPanel{border:1px solid #444;border-radius:3px;}");
	auto *l = new QVBoxLayout(box);
	l->setContentsMargins(6, 4, 6, 4);
	l->setSpacing(3);

	auto *info = new QHBoxLayout;
	info->addWidget(new QLabel("<b>REPLAY CUE</b> [9 cue · 0 highlights]", box));
	rcStatus_ = new QLabel(box);
	rcStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	rcInfo_ = new QLabel(box);
	info->addWidget(rcStatus_, 1);
	info->addWidget(rcInfo_);
	l->addLayout(info);

	auto button = [&](const QString &text, const QString &tip, QHBoxLayout *row, std::function<void()> fn) {
		QPushButton *b = makeButton(text, box);
		b->setToolTip(tip);
		row->addWidget(b, 1);
		connect(b, &QPushButton::clicked, this, fn);
		return b;
	};
	auto *row1 = new QHBoxLayout;
	button("Instant Replay", "Save the last seconds from every camera as a new replay (Replay Cue)", row1,
	       [this]() {
		       OBSDataAutoRelease d = obs_data_create();
		       obs_data_set_bool(d, "play", false);
		       controller_->replayCueAction("InstantReplay", d);
	       });
	button("Mark IN", "Mark the start of a replay", row1, [this]() { controller_->replayCueAction("MarkIn"); });
	button("Mark OUT", "Mark the end and save the replay", row1,
	       [this]() { controller_->replayCueAction("MarkOut"); });
	button("◀ Prev", "Select the previous replay in Replay Cue's list", row1,
	       [this]() { controller_->replayCueSelectRelative(-1); });
	button("Cue [9]", "Load the selected replay paused into Preview (then CUT)", row1,
	       [this]() { controller_->selectSlot(kSlotReplay); });
	button("Next ▶", "Select the next replay in Replay Cue's list", row1,
	       [this]() { controller_->replayCueSelectRelative(1); });
	rcPlayPause_ = button("Play / Pause", "Pause or resume the replay player", row1,
			      [this]() { controller_->replayCueAction("PlayPause"); });
	l->addLayout(row1);

	auto *row2 = new QHBoxLayout;
	for (int pct : {25, 50, 75, 100})
		button(QString("%1%").arg(pct), "Replay speed (slow motion)", row2, [this, pct]() {
			OBSDataAutoRelease d = obs_data_create();
			obs_data_set_double(d, "percent", pct);
			controller_->replayCueAction("SetSpeed", d);
		});
	button("◀ Angle", "Previous camera angle", row2, [this]() { controller_->replayCueAction("PrevAngle"); });
	button("Angle ▶", "Next camera angle", row2, [this]() { controller_->replayCueAction("NextAngle"); });
	button("Highlights [0]", "Stage the highlights reel in Preview (then CUT plays it)", row2,
	       [this]() { controller_->selectSlot(kSlotHighlights); });
	rcPlayHighlights_ =
		button("Play Highlights (LIVE)",
		       "Immediately put the current break's highlights reel on air (Replay Cue's transition)", row2,
		       [this]() { controller_->replayCueAction("PlayHighlights"); });
	rcPlayHighlights_->setStyleSheet("QPushButton{background:#7a1010;color:white;}");
	l->addLayout(row2);
	return box;
}

void MultiviewWindow::updateReplayCuePanel()
{
	const ReplayCueStatus &st = controller_->replayCue().status();
	rcStatus_->setText(QString::fromStdString(describeReplayCuePlayback(st)));
	rcStatus_->setStyleSheet(!st.ok                  ? "QLabel{color:#ff8a65;}"
				 : st.state == "playing" ? "QLabel{color:#ff8a80;font-weight:bold;}"
				 : st.cued               ? "QLabel{color:#81c784;font-weight:bold;}"
							 : "");
	QStringList info;
	if (st.ok) {
		info << (st.armed ? "ARMED" : "not armed");
		if (st.eventCount >= 0)
			info << QString("%1 replays").arg(st.eventCount);
		if (st.pendingReplays > 0)
			info << QString("%1 saving").arg(st.pendingReplays);
		if (!st.phaseName.empty())
			info << QString::fromStdString(st.phaseName);
	}
	rcInfo_->setText(info.join(" · "));
	const auto names = controller_->replayCueScenes();
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	const std::string pg = nameOf(program);
	const bool live = !pg.empty() && (pg == names.first || pg == names.second);
	rcPlayPause_->setText(live ? "Play / Pause (LIVE)" : "Play / Pause");
	rcPlayPause_->setStyleSheet(live ? "QPushButton{background:#7a1010;color:white;}" : "");
	for (QPushButton *b : rcPanel_->findChildren<QPushButton *>())
		b->setEnabled(st.ok);
}

void MultiviewWindow::replayCuePanes(std::array<PaneSpec, kPaneCount> &panes, const SwitchView &sv)
{
	const ReplayCueStatus &st = controller_->replayCue().status();
	const auto names = controller_->replayCueScenes();
	const bool sameScene = names.first == names.second;
	const bool staged = controller_->replayCueHighlightsStaged();

	PaneSpec &r = panes[kPaneReplay];
	if (!names.first.empty())
		r.source = obs_get_source_by_name(names.first.c_str());
	r.title = "REPLAY CUE"; // the status line carries the replay name, angle, speed and time
	r.status = QString::fromStdString(describeReplayCuePlayback(st));
	r.preview = !names.first.empty() && sv.previewName == names.first && !(sameScene && staged);
	r.live = !names.first.empty() && sv.programName == names.first && !(sameScene && st.highlights);
	r.statusColor = st.state == "playing" ? 0xFFFF8A80 : st.cued ? 0xFF81C784 : 0xFFB0BEC5;

	PaneSpec &h = panes[kPaneHighlights];
	if (!names.second.empty())
		h.source = obs_get_source_by_name(names.second.c_str());
	h.title = QString("HIGHLIGHTS · %1")
			  .arg(st.phaseName.empty() ? QString("all highlights") : QString::fromStdString(st.phaseName));
	h.status = st.highlights
			   ? QString("PLAYING REEL · %1").arg(QString::fromStdString(describeReplayCuePlayback(st)))
		   : st.inBreak ? QString("BREAK · reel ready")
				: QString("reel builds during the match");
	h.preview = !names.second.empty() && sv.previewName == names.second && (staged || !sameScene);
	h.live = !names.second.empty() && sv.programName == names.second && (st.highlights || !sameScene);
	h.statusColor = st.highlights ? 0xFFFF8A80 : st.inBreak ? 0xFF81C784 : 0xFFB0BEC5;

	if (!st.ok) {
		r.status = h.status = QString::fromStdString(st.error.empty() ? "Replay Cue not detected" : st.error);
		r.statusColor = h.statusColor = 0xFFFF7043;
	} else if (names.first.empty()) {
		r.status = h.status = "Replay Cue scene not created yet (play once in Replay Cue)";
	}
	r.dimmed = !st.ok || !r.source;
	h.dimmed = !st.ok || !h.source;
}

void MultiviewWindow::chooseFile(int slot)
{
	PlaybackView v = controller_->playback().view(slot);
	if (!v.canModify) {
		controller_->setMessage(QString::fromStdString(v.modifyBlockReason), true);
		return;
	}
	QString start = v.file.empty() ? QString() : QFileInfo(QString::fromStdString(v.file)).absolutePath();
	QString path = QFileDialog::getOpenFileName(
		this, slot == kSlotReplay ? "Load replay clip" : "Load highlights reel", start,
		"Media files (*.mp4 *.mov *.mkv *.m4v *.ts *.webm *.avi *.mxf *.flv);;All files (*)");
	if (!path.isEmpty())
		controller_->loadPlayback(slot, path);
}

void MultiviewWindow::showAndRaise()
{
	show();
	raise();
	activateWindow();
	refresh();
}

void MultiviewWindow::releaseRendering()
{
	if (surface_)
		surface_->releaseSources();
}

bool MultiviewWindow::typingWidgetFocused() const
{
	// Any modal dialog (rename, file picker) owns the keyboard.
	if (QApplication::activeModalWidget())
		return true;
	QWidget *w = QApplication::focusWidget();
	if (!w || w->window() != this)
		return false; // focus is in another window (OBS, Setup): it gets its own keys
	if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w) ||
	    qobject_cast<QAbstractSpinBox *>(w))
		return true;
	if (qobject_cast<QComboBox *>(w))
		return true; // pickers: arrow / number keys must not switch cameras
	return false;
}

void MultiviewWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->isAutoRepeat()) {
		event->accept(); // never repeat takes from a held key
		return;
	}
	if (typingWidgetFocused() ||
	    (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
		QWidget::keyPressEvent(event);
		return;
	}
	const int k = event->key();
	if (k >= Qt::Key_1 && k <= Qt::Key_8) {
		controller_->selectSlot(k - Qt::Key_1);
	} else if (k == Qt::Key_9) {
		controller_->selectSlot(kSlotReplay);
	} else if (k == Qt::Key_0) {
		controller_->selectSlot(kSlotHighlights);
	} else if (k == Qt::Key_Space) {
		controller_->cut();
	} else if (k == Qt::Key_Escape) {
		if (isFullScreen())
			toggleFullScreen();
	} else {
		QWidget::keyPressEvent(event);
		return;
	}
	event->accept();
}

void MultiviewWindow::closeEvent(QCloseEvent *event)
{
	// Closing only hides the multiview: OBS, Program and outputs keep running.
	saveGeometryPrefs();
	event->accept();
}

void MultiviewWindow::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	releaseRendering();
}

void MultiviewWindow::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refresh();
}

void MultiviewWindow::toggleFullScreen()
{
	if (isFullScreen()) {
		if (wasMaximized_)
			showMaximized();
		else
			showNormal();
	} else {
		wasMaximized_ = isMaximized();
		showFullScreen();
	}
	fullScreen_->setText(isFullScreen() ? "Exit Full Screen [Esc]" : "Full Screen");
}

void MultiviewWindow::openSetup()
{
	if (!setupDialog_)
		setupDialog_ = new SetupDialog(controller_, this);
	setupDialog_->reload();
	setupDialog_->show();
	setupDialog_->raise();
	setupDialog_->activateWindow();
}

void MultiviewWindow::onPaneClicked(int pane)
{
	if (paneIsCamera(pane))
		controller_->selectSlot(pane - kPaneCam1);
	else if (pane == kPaneReplay)
		controller_->selectSlot(kSlotReplay);
	else if (pane == kPaneHighlights)
		controller_->selectSlot(kSlotHighlights);
	setFocus();
}

void MultiviewWindow::onPaneDoubleClicked(int pane)
{
	int slot = paneIsCamera(pane)        ? pane - kPaneCam1
		   : pane == kPaneReplay     ? kSlotReplay
		   : pane == kPaneHighlights ? kSlotHighlights
					     : -1;
	if (slot < 0)
		return;
	bool ok = false;
	QString label = QInputDialog::getText(this, "Tile label", "Label:", QLineEdit::Normal,
					      controller_->slotLabel(slot), &ok);
	if (ok)
		controller_->setSlotLabel(slot, label.trimmed());
}

void MultiviewWindow::onMessage(const QString &message, bool error)
{
	if (message.isEmpty()) {
		message_->clear();
		return;
	}
	message_->setText(QString("%1  %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), message));
	message_->setStyleSheet(error ? "QLabel{color:#ff8a65;}" : "QLabel{color:#9ccc65;}");
}

// ---------------------------------------------------------------------------

void MultiviewWindow::refresh()
{
	if (!isVisible())
		return;
	updateToolbar();
	const bool rc = controller_->replayCueMode();
	builtinPanels_->setVisible(!rc);
	rcPanel_->setVisible(rc);
	if (rc) {
		updateReplayCuePanel();
	} else {
		updatePlayback(kSlotReplay, pb_[0]);
		updatePlayback(kSlotHighlights, pb_[1]);
	}
	updatePanes();
	surface_->purgeGraveyard();
}

static QString colorBadge(const char *bg)
{
	return QString("QLabel{background:%1;color:white;font-weight:bold;padding:2px 6px;}").arg(bg);
}

void MultiviewWindow::updateToolbar()
{
	Controller *c = controller_;
	if (!c->collectionReady()) {
		cut_->setEnabled(false);
		returnLive_->setEnabled(false);
		state_->setText("Scene collection loading – switching suspended");
		return;
	}
	SwitchView sv = c->switcher().view();

	cut_->setEnabled(sv.cutEnabled);
	cut_->setToolTip(sv.cutEnabled ? "Hard cut Preview → Program" : QString::fromStdString(sv.cutReason));
	studio_->setVisible(!sv.studioMode);

	QString rl = "RETURN LIVE";
	if (sv.lastCameraSlot >= 0)
		rl += QString(" → CAM %1").arg(sv.lastCameraSlot + 1);
	returnLive_->setText(rl);
	returnLive_->setEnabled(sv.returnLiveEnabled);
	returnLive_->setToolTip(sv.returnLiveEnabled ? "Immediate on-air cut to the last camera that was on Program"
						     : QString::fromStdString(sv.returnLiveReason));
	returnLive_->setStyleSheet(sv.returnLiveEnabled && slotIsPlayback(sv.programSlot)
					   ? "QPushButton{background:#7a4a00;color:white;font-weight:bold;}"
					   : "");

	GraphicsVisibility gv = c->graphics().visibility();
	graphics_->setText(QString("Camera Graphics: %1").arg(graphicsVisibilityName(gv)));
	graphics_->setEnabled(gv == GraphicsVisibility::On || gv == GraphicsVisibility::Off ||
			      gv == GraphicsVisibility::Mixed);
	graphics_->setToolTip(gv == GraphicsVisibility::Mixed
				      ? "Items differ between camera scenes – click to apply ON to all eight"
			      : gv == GraphicsVisibility::Unavailable
				      ? "Browser Source is not available in this OBS installation"
				      : "Show / hide the shared MatchFrame overlay in all eight camera scenes");

	transition_->setText(
		sv.transitionIsCut
			? "Transition: Cut"
			: QString("Transition: %1 (CUT selects Cut)").arg(QString::fromStdString(sv.transitionName)));
	QString st;
	if (!sv.studioMode)
		st = "STUDIO MODE OFF – plugin takes disabled";
	else if (sv.busy)
		st = "TAKING…";
	else if (sv.transitionActive)
		st = "TRANSITION RUNNING";
	state_->setText(st);

	const bool streaming = c->outputsStreaming(), recording = c->outputsRecording();
	streaming_->setText(streaming ? "STREAMING" : "stream off");
	streaming_->setStyleSheet(streaming ? colorBadge("#c62828") : "QLabel{color:#888;}");
	recording_->setText(recording ? "RECORDING" : "rec off");
	recording_->setStyleSheet(recording ? colorBadge("#c62828") : "QLabel{color:#888;}");

	QStringList warnings;
	if (!c->bank().anySceneExists())
		warnings << "MatchFrame scenes are not set up – open Setup… and run Create / Repair.";
	if (studioDuplicateSourcesEnabled())
		warnings << "Studio Mode 'Duplicate Sources' is ON: OBS would copy sources on each take (reloading "
			    "graphics / desynchronising media). Turn it off in the Studio Mode transition menu.";
	if (!c->graphics().browserSupported())
		warnings
			<< "Browser Source is unavailable in this OBS – camera graphics disabled (video-only operation).";
	if (!surface_->displayError().isEmpty())
		warnings << surface_->displayError();
	warning_->setText(warnings.join("  "));
	warning_->setVisible(!warnings.isEmpty());
}

static QString stateColor(PlaybackState s)
{
	switch (s) {
	case PlaybackState::Ready:
		return "#2e7d32";
	case PlaybackState::Playing:
		return "#c62828";
	case PlaybackState::Paused:
		return "#6d4c41";
	case PlaybackState::Ended:
		return "#455a64";
	case PlaybackState::Error:
		return "#e65100";
	case PlaybackState::Loading:
		return "#1565c0";
	default:
		return "#424242";
	}
}

void MultiviewWindow::updatePlayback(int slot, PlaybackUi &ui)
{
	if (!controller_->collectionReady()) {
		for (QPushButton *b : {ui.load, ui.arm, ui.play, ui.restart})
			b->setEnabled(false);
		return;
	}
	PlaybackView v = controller_->playback().view(slot);
	ui.file->setText(v.fileName.empty() ? "<i>no file</i>" : QString::fromStdString(v.fileName).toHtmlEscaped());
	ui.file->setToolTip(QString::fromStdString(v.file));
	QString st = playbackStateName(v.state);
	if (v.onProgram)
		st += " · ON PROGRAM";
	else if (v.activeElsewhere)
		st += " · ACTIVE ELSEWHERE";
	ui.status->setText(st);
	ui.status->setStyleSheet(colorBadge(stateColor(v.state).toUtf8().constData()));
	ui.status->setToolTip(QString::fromStdString(v.error));
	ui.time->setText(v.durationMs > 0 ? QString("%1 / %2").arg(QString::fromStdString(formatClock(v.timeMs)),
								   QString::fromStdString(formatClock(v.durationMs)))
					  : QString("–:– / –:–"));

	const bool loaded = !v.file.empty() && v.state != PlaybackState::Empty && v.state != PlaybackState::Error;
	ui.load->setEnabled(v.sourceExists && v.canModify);
	ui.load->setToolTip(v.canModify ? "Choose a prepared file and arm it at its first frame (off-air)"
					: QString::fromStdString(v.modifyBlockReason));
	ui.arm->setEnabled(v.sourceExists && v.canModify && !v.file.empty() && v.state != PlaybackState::Ready &&
			   v.state != PlaybackState::Loading);

	const bool playing = v.state == PlaybackState::Playing;
	const QString live = v.onProgram ? " (LIVE)" : "";
	ui.play->setText((playing ? "Pause" : "Play") + live);
	ui.restart->setText("Restart" + live);
	ui.play->setEnabled(loaded);
	ui.restart->setEnabled(loaded);
	const QString liveStyle = v.onProgram ? "QPushButton{background:#7a1010;color:white;}" : "";
	ui.play->setStyleSheet(liveStyle);
	ui.restart->setStyleSheet(liveStyle);
	ui.play->setToolTip(v.onProgram ? "Affects the LIVE output"
					: "Off-air transport (disarms; Arm again before a take)");
	ui.restart->setToolTip(v.onProgram ? "Restarts the clip on the LIVE output" : ui.play->toolTip());
}

void MultiviewWindow::updatePanes()
{
	std::array<PaneSpec, kPaneCount> panes;
	Controller *c = controller_;
	if (!c->collectionReady()) {
		surface_->releaseSources();
		return;
	}
	SwitchView sv = c->switcher().view();
	SceneBank &bank = c->bank();

	PaneSpec &pv = panes[kPanePreview];
	pv.preview = true;
	if (sv.studioMode) {
		pv.source = obs_frontend_get_current_preview_scene();
		pv.title = QString("PREVIEW — %1").arg(QString::fromStdString(sv.previewName));
	} else {
		pv.title = "PREVIEW — Studio Mode is off";
		pv.dimmed = true;
	}

	PaneSpec &pg = panes[kPaneProgram];
	pg.renderProgram = true;
	pg.live = true;
	pg.title = QString("PROGRAM — %1").arg(QString::fromStdString(sv.programName));
	if (sv.transitionActive)
		pg.status = "TRANSITION";

	for (int cam = 0; cam < 8; cam++) {
		PaneSpec &p = panes[paneForCamera(cam)];
		SlotFacts f = c->switcher().slotFacts(cam);
		const CameraSlotConfig &cc = c->config().cameras[cam];
		p.source = bank.slotScene(cam);
		p.title = QString("%1  %2").arg(cam + 1).arg(QString::fromStdString(cc.label));
		p.preview = sv.previewSlot == cam;
		p.live = sv.programSlot == cam;
		if (!f.sceneExists) {
			p.status = "NO SCENE";
			p.statusColor = 0xFFFFB74D;
			p.dimmed = true;
		} else if (!f.inputAssigned) {
			p.status = "UNASSIGNED";
			p.statusColor = 0xFF9E9E9E;
			p.dimmed = true;
		} else if (!f.inputExists) {
			p.status = "MISSING INPUT";
			p.statusColor = 0xFFFF7043;
			p.dimmed = true;
		} else {
			auto shared = bank.slotsSharingInput(cam);
			p.status = QString::fromStdString(cc.inputName);
			if (!shared.empty()) {
				p.status = "SAME FEED AS CAM " + QString::number(shared.front() + 1);
				p.statusColor = 0xFFFFD54F;
			} else {
				p.statusColor = 0xFFB0BEC5;
			}
		}
	}

	for (int slot : {kSlotReplay, kSlotHighlights}) {
		if (c->replayCueMode())
			break;
		PaneSpec &p = panes[slot == kSlotReplay ? kPaneReplay : kPaneHighlights];
		PlaybackView v = c->playback().view(slot);
		p.source = bank.slotScene(slot);
		p.title = QString("%1 · %2").arg(slot == kSlotReplay ? "REPLAY" : "HIGHLIGHTS",
						 QString::fromStdString(c->config().playback(slot).label));
		p.preview = sv.previewSlot == slot;
		p.live = sv.programSlot == slot;
		QString status = playbackStateName(v.state);
		if (!v.fileName.empty())
			status += "  " + QString::fromStdString(v.fileName);
		if (v.durationMs > 0)
			status += QString("  %1/%2").arg(QString::fromStdString(formatClock(v.timeMs)),
							 QString::fromStdString(formatClock(v.durationMs)));
		p.status = status;
		switch (v.state) {
		case PlaybackState::Ready:
			p.statusColor = 0xFF81C784;
			break;
		case PlaybackState::Playing:
			p.statusColor = 0xFFFF8A80;
			break;
		case PlaybackState::Error:
			p.statusColor = 0xFFFF7043;
			break;
		default:
			p.statusColor = 0xFFB0BEC5;
		}
		p.dimmed = !p.source || v.state == PlaybackState::Empty || v.state == PlaybackState::Error;
	}

	if (c->replayCueMode())
		replayCuePanes(panes, sv);

	surface_->setPanes(std::move(panes));
}

// ---------------------------------------------------------------------------
// window preferences (module config, separate from the scene collection)

static std::string windowPrefsPath()
{
	char *p = obs_module_config_path("window.json");
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

void MultiviewWindow::saveGeometryPrefs()
{
	std::string path = windowPrefsPath();
	if (path.empty())
		return;
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_string(d, "geometry", saveGeometry().toBase64().constData());
	obs_data_set_bool(d, "fullscreen", isFullScreen());
	obs_data_save_json_safe(d, path.c_str(), "tmp", "bak");
}

void MultiviewWindow::restoreGeometryPrefs()
{
	resize(1600, 900);
	std::string path = windowPrefsPath();
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!d)
		return;
	const char *g = obs_data_get_string(d, "geometry");
	if (g && *g)
		restoreGeometry(QByteArray::fromBase64(g));
}

} // namespace mf
