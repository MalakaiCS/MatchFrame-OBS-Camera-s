// MatchFrame Multiview - single native OBS display rendering twelve panes.
//
// Threading model:
//  * The UI thread builds an immutable RenderSnapshot (retained source references,
//    showing-state increments, label textures, layout) and publishes it.
//  * The OBS graphics thread's draw callback copies the current snapshot pointer under
//    a short lock and draws from it. It never calls frontend APIs, Qt or the network.
//  * Replaced snapshots go to a graveyard that is purged on the UI thread, so the
//    final release of sources / textures (and the showing decrement) always happens on
//    the UI thread, never inside the draw callback.
#pragma once

#include "core/layout.hpp"

#include <obs.h>
#include <obs.hpp>

#include <QImage>
#include <QWidget>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mf {

// A GPU texture created from a QImage on the UI thread.
class LabelTexture {
public:
	explicit LabelTexture(const QImage &img);
	~LabelTexture();
	LabelTexture(const LabelTexture &) = delete;
	LabelTexture &operator=(const LabelTexture &) = delete;

	gs_texture_t *texture() const { return tex_; }
	uint32_t width() const { return w_; }
	uint32_t height() const { return h_; }

private:
	gs_texture_t *tex_ = nullptr;
	uint32_t w_ = 0, h_ = 0;
};

// Description of one pane, produced by the window from controller state.
struct PaneSpec {
	OBSSourceAutoRelease source;       // scene to render (nullptr = nothing / program)
	bool renderProgram = false;        // render OBS's main output texture
	bool preview = false;              // PREVIEW tally
	bool live = false;                 // LIVE / Program tally
	bool dimmed = false;               // unassigned / invalid
	QString title;                     // left label text
	QString status;                    // right-aligned status text
	uint32_t statusColor = 0xFFDDDDDD; // QRgb
};

struct RenderSnapshot {
	Layout layout;
	uint32_t canvasW = 0, canvasH = 0;
	std::array<obs_source_t *, kPaneCount> sources{}; // retained
	std::array<bool, kPaneCount> renderProgram{};
	std::array<bool, kPaneCount> preview{};
	std::array<bool, kPaneCount> live{};
	std::array<bool, kPaneCount> dimmed{};
	std::array<std::shared_ptr<LabelTexture>, kPaneCount> labels{};
	std::vector<obs_source_t *> showing; // unique sources whose showing count we hold

	RenderSnapshot() = default;
	RenderSnapshot(const RenderSnapshot &) = delete;
	RenderSnapshot &operator=(const RenderSnapshot &) = delete;
	~RenderSnapshot();
};

class RenderSurface : public QWidget {
	Q_OBJECT

public:
	explicit RenderSurface(QWidget *parent);
	~RenderSurface() override;

	QPaintEngine *paintEngine() const override { return nullptr; }

	// Publishes a new set of panes (UI thread). Cheap when nothing changed.
	void setPanes(std::array<PaneSpec, kPaneCount> &&panes);
	// Drops the snapshot and every source reference held for rendering.
	void releaseSources();

	bool displayValid() const { return display_ != nullptr; }
	QString displayError() const { return displayError_; }
	const Layout &logicalLayout() const { return logicalLayout_; }

	// Purges graveyard snapshots no longer used by the render thread. Returns remaining.
	size_t purgeGraveyard();
	uint64_t framesDrawn() const { return framesDrawn_; }

signals:
	void paneClicked(int pane);
	void paneDoubleClicked(int pane);

private slots:
	void onScreenChanged(QScreen *screen);

protected:
	void paintEvent(QPaintEvent *) override {}
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool event(QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	void rebuild();
	QSize physicalSize() const;
	std::shared_ptr<LabelTexture> labelFor(const PaneSpec &spec, int pane, int w, int h);
	static void draw(void *data, uint32_t cx, uint32_t cy);
	void drawInternal(uint32_t cx, uint32_t cy);

	obs_display_t *display_ = nullptr;
	QString displayError_;
	std::array<PaneSpec, kPaneCount> panes_;
	Layout logicalLayout_;

	std::mutex snapMutex_;
	std::shared_ptr<RenderSnapshot> snapshot_;
	std::vector<std::shared_ptr<RenderSnapshot>> graveyard_;
	std::unordered_map<std::string, std::weak_ptr<LabelTexture>> labelCache_;
	std::atomic<uint64_t> framesDrawn_{0};
};

} // namespace mf
