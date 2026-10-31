// MatchFrame Multiview - setup dialog (assignments, graphics URLs, playback files, repair).
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <array>

namespace mf {

class Controller;

class SetupDialog : public QDialog {
	Q_OBJECT

public:
	SetupDialog(Controller *controller, QWidget *parent);
	void reload();

private:
	struct CamRow {
		QLabel *slot = nullptr;
		QLineEdit *label = nullptr;
		QComboBox *input = nullptr;
		QLabel *scene = nullptr;
		QLabel *warning = nullptr;
		QPushButton *fit = nullptr;
		QPushButton *fill = nullptr;
	};
	struct PbRow {
		QLineEdit *label = nullptr;
		QLabel *file = nullptr;
		QPushButton *browse = nullptr;
		QLabel *state = nullptr;
		QLabel *scene = nullptr;
		QLineEdit *overlayUrl = nullptr;
		QCheckBox *overlayOn = nullptr;
		QPushButton *overlayApply = nullptr;
	};

	struct TransitionRow {
		QLabel *name = nullptr;
		QComboBox *transition = nullptr;
		QSpinBox *duration = nullptr;
	};

	void build();
	void reloadTransitions();
	void applyTransition(int slot);
	void refreshStatus();
	void applyLabels();
	void repair();

	Controller *controller_;
	std::array<CamRow, 8> cams_;
	std::array<PbRow, 3> pbs_; // Replay, Highlights, Video Ads
	QLineEdit *graphicsUrl_ = nullptr;
	QPushButton *showUrl_ = nullptr;
	QPushButton *applyUrl_ = nullptr;
	QLabel *graphicsState_ = nullptr;
	QCheckBox *programOnly_ = nullptr;
	std::array<TransitionRow, 12> transitions_; // one per bank slot
	QLabel *intermissionScene_ = nullptr;
	QLineEdit *intermissionLabel_ = nullptr;
	QLineEdit *intermissionUrl_ = nullptr;
	QPushButton *intermissionApply_ = nullptr;
	QLabel *canvas_ = nullptr;
	QPushButton *repair_ = nullptr;
	QLabel *repairNote_ = nullptr;
	QPlainTextEdit *log_ = nullptr;
	QComboBox *provider_ = nullptr;
	QLabel *rcState_ = nullptr;
	QComboBox *rcReplayScene_ = nullptr;
	QComboBox *rcHighlightsScene_ = nullptr;
	void reloadReplayCueScenes();
	bool loading_ = false;
};

} // namespace mf
