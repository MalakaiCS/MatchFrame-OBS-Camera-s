// MatchFrame Multiview - the operator window (Tools → MatchFrame Multiview).
#pragma once

#include "render-surface.hpp"

#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

namespace mf {

class Controller;
class SetupDialog;

class MultiviewWindow : public QWidget {
	Q_OBJECT

public:
	MultiviewWindow(Controller *controller, QWidget *parent);
	~MultiviewWindow() override;

	void showAndRaise();
	void releaseRendering();
	void saveGeometryPrefs();
	void toggleFullScreen();
	RenderSurface *surface() const { return surface_; }
	void openSetup();

	// True when a text-entry widget has focus (local shortcuts are suppressed).
	bool typingWidgetFocused() const;

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void showEvent(QShowEvent *event) override;

private slots:
	void refresh();
	void onPaneClicked(int pane);
	void onPaneDoubleClicked(int pane);
	void onMessage(const QString &message, bool error);

private:
	struct PlaybackUi {
		QLabel *title = nullptr;
		QLabel *file = nullptr;
		QLabel *status = nullptr;
		QLabel *time = nullptr;
		QPushButton *load = nullptr;
		QPushButton *arm = nullptr;
		QPushButton *play = nullptr;
		QPushButton *restart = nullptr;
	};

	void buildUi();
	QWidget *buildReplayCuePanel();
	void updateReplayCuePanel();
	void replayCuePanes(std::array<PaneSpec, kPaneCount> &panes, const struct SwitchView &sv);
	QWidget *buildPlaybackPanel(int slot, PlaybackUi &ui);
	void updatePanes();
	void updateToolbar();
	void updatePlayback(int slot, PlaybackUi &ui);
	void restoreGeometryPrefs();
	void chooseFile(int slot);

	Controller *controller_;
	RenderSurface *surface_ = nullptr;
	QPushButton *cut_ = nullptr;
	QPushButton *returnLive_ = nullptr;
	QPushButton *graphics_ = nullptr;
	QPushButton *setup_ = nullptr;
	QPushButton *fullScreen_ = nullptr;
	QPushButton *studio_ = nullptr;
	QLabel *transition_ = nullptr;
	QLabel *streaming_ = nullptr;
	QLabel *recording_ = nullptr;
	QLabel *state_ = nullptr;
	QLabel *message_ = nullptr;
	QLabel *warning_ = nullptr;
	PlaybackUi pb_[2];
	QWidget *builtinPanels_ = nullptr;
	QWidget *rcPanel_ = nullptr;
	QLabel *rcStatus_ = nullptr;
	QLabel *rcInfo_ = nullptr;
	QPushButton *rcPlayPause_ = nullptr;
	QPushButton *rcPlayHighlights_ = nullptr;
	QPointer<SetupDialog> setupDialog_;
	bool wasMaximized_ = false;
};

} // namespace mf
