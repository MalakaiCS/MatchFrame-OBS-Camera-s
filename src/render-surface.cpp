#include "render-surface.hpp"

#include "clean-feed.hpp"

#include "obs-util.hpp"

#include <plugin-support.h>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#ifdef __linux__
#include <obs-nix-platform.h>
#ifdef MF_HAVE_QPA
#include <qpa/qplatformnativeinterface.h>
#endif
#endif

#include <algorithm>
#include <unordered_set>

namespace mf {

// ---------------------------------------------------------------------------
// LabelTexture

LabelTexture::LabelTexture(const QImage &src)
{
	QImage img = src.convertToFormat(QImage::Format_ARGB32); // BGRA in memory (little endian)
	w_ = (uint32_t)img.width();
	h_ = (uint32_t)img.height();
	std::vector<uint8_t> packed((size_t)w_ * h_ * 4);
	for (uint32_t y = 0; y < h_; y++)
		memcpy(packed.data() + (size_t)y * w_ * 4, img.constScanLine((int)y), (size_t)w_ * 4);
	const uint8_t *planes[] = {packed.data()};
	obs_enter_graphics();
	tex_ = gs_texture_create(w_, h_, GS_BGRA, 1, planes, 0);
	obs_leave_graphics();
}

LabelTexture::~LabelTexture()
{
	if (tex_) {
		obs_enter_graphics();
		gs_texture_destroy(tex_);
		obs_leave_graphics();
	}
}

RenderSnapshot::~RenderSnapshot()
{
	for (obs_source_t *s : showing)
		obs_source_dec_showing(s);
	for (obs_source_t *s : sources)
		if (s)
			obs_source_release(s);
}

// ---------------------------------------------------------------------------
// native window -> gs_window

static bool toGsWindow(QWindow *window, gs_window &gsw, QString &err)
{
	if (!window) {
		err = "no native window";
		return false;
	}
#if defined(_WIN32)
	gsw.hwnd = (void *)window->winId();
	return true;
#elif defined(__APPLE__)
	gsw.view = (id)window->winId();
	return true;
#else
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		gsw.id = (uint32_t)window->winId();
		gsw.display = obs_get_nix_platform_display();
		return true;
	case OBS_NIX_PLATFORM_WAYLAND: {
#ifdef MF_HAVE_QPA
		QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
		gsw.display = native ? native->nativeResourceForWindow("surface", window) : nullptr;
		if (!gsw.display)
			err = "Wayland surface unavailable";
		return gsw.display != nullptr;
#else
		err = "built without Wayland display support";
		return false;
#endif
	}
	default:
		err = "unsupported windowing platform";
		return false;
	}
#endif
}

// ---------------------------------------------------------------------------

RenderSurface::RenderSurface(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(false);
	setMinimumSize(320, 240);
}

RenderSurface::~RenderSurface()
{
	destroyDisplay();
	releaseSources();
	purgeGraveyard();
}

QSize RenderSurface::physicalSize() const
{
	const qreal dpr = devicePixelRatioF();
	return QSize(std::max(1, (int)(width() * dpr)), std::max(1, (int)(height() * dpr)));
}

void RenderSurface::createDisplay()
{
	if (display_ || !isVisible())
		return;
	winId(); // force the native window
	QSize ps = physicalSize();
	gs_init_data info = {};
	info.cx = (uint32_t)ps.width();
	info.cy = (uint32_t)ps.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	QString err;
	if (!toGsWindow(windowHandle(), info.window, err)) {
		displayError_ = "Cannot create video display: " + err;
		obs_log(LOG_ERROR, "%s", displayError_.toUtf8().constData());
		return;
	}
	display_ = obs_display_create(&info, 0xFF141414);
	if (!display_) {
		displayError_ = "obs_display_create failed";
		obs_log(LOG_ERROR, "%s", displayError_.toUtf8().constData());
		return;
	}
	displayError_.clear();
	obs_display_add_draw_callback(display_, &RenderSurface::draw, this);
	if (windowHandle())
		connect(windowHandle(), &QWindow::screenChanged, this, &RenderSurface::onScreenChanged,
			Qt::UniqueConnection);
	obs_log(LOG_INFO, "multiview display created (%dx%d physical)", ps.width(), ps.height());
}

void RenderSurface::onScreenChanged(QScreen *)
{
	if (display_) {
		QSize s = physicalSize();
		obs_display_resize(display_, (uint32_t)s.width(), (uint32_t)s.height());
	}
	rebuild();
}

void RenderSurface::destroyDisplay()
{
	if (!display_)
		return;
	// Remove the callback first; obs_display_destroy then waits for any draw in flight.
	obs_display_remove_draw_callback(display_, &RenderSurface::draw, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void RenderSurface::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	createDisplay();
	rebuild();
}

void RenderSurface::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	// Hiding releases only multiview-owned references; scenes, Program and graphics
	// keep running inside OBS.
	destroyDisplay();
	releaseSources();
}

void RenderSurface::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (!display_)
		createDisplay();
	if (display_) {
		QSize s = physicalSize();
		obs_display_resize(display_, (uint32_t)s.width(), (uint32_t)s.height());
	}
	rebuild();
}

bool RenderSurface::event(QEvent *e)
{
	if (e->type() == QEvent::ScreenChangeInternal || e->type() == QEvent::WindowStateChange) {
		if (display_) {
			QSize s = physicalSize();
			obs_display_resize(display_, (uint32_t)s.width(), (uint32_t)s.height());
		}
		rebuild();
	}
	return QWidget::event(e);
}

void RenderSurface::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	const qreal dpr = devicePixelRatioF();
	double px, py;
	logicalToPhysical(dpr, event->position().x(), event->position().y(), px, py);
	std::shared_ptr<RenderSnapshot> snap;
	{
		std::lock_guard<std::mutex> lock(snapMutex_);
		snap = snapshot_;
	}
	if (!snap)
		return;
	int pane = hitTest(snap->layout, px, py);
	if (pane >= 0)
		emit paneClicked(pane);
}

void RenderSurface::mouseDoubleClickEvent(QMouseEvent *event)
{
	const qreal dpr = devicePixelRatioF();
	double px, py;
	logicalToPhysical(dpr, event->position().x(), event->position().y(), px, py);
	std::shared_ptr<RenderSnapshot> snap;
	{
		std::lock_guard<std::mutex> lock(snapMutex_);
		snap = snapshot_;
	}
	if (!snap)
		return;
	int pane = hitTest(snap->layout, px, py);
	if (pane >= 0)
		emit paneDoubleClicked(pane);
}

static std::string paneSignature(const PaneSpec &p)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%p|%d%d%d%d|%08x|", (void *)p.source.Get(), p.renderProgram, p.preview, p.live,
		 p.dimmed, p.statusColor);
	return std::string(buf) + p.title.toStdString() + "|" + p.status.toStdString();
}

void RenderSurface::setPanes(std::array<PaneSpec, kPaneCount> &&panes)
{
	bool changed = !snapshot_;
	for (int i = 0; i < kPaneCount && !changed; i++)
		changed = paneSignature(panes[i]) != paneSignature(panes_[i]);
	panes_ = std::move(panes);
	if (changed)
		rebuild();
	purgeGraveyard();
}

void RenderSurface::releaseSources()
{
	// Removing the draw callback waits for a draw in flight to finish, so afterwards the
	// graphics thread holds no snapshot and every reference is released right here.
	// (Required before OBS clears a scene collection: a surviving reference would make
	// OBS report orphaned sources and stop saving.)
	if (display_)
		obs_display_remove_draw_callback(display_, &RenderSurface::draw, this);
	std::shared_ptr<RenderSnapshot> old;
	{
		std::lock_guard<std::mutex> lock(snapMutex_);
		old = std::move(snapshot_);
		snapshot_.reset();
	}
	if (old)
		graveyard_.push_back(std::move(old));
	for (auto &p : panes_)
		p = PaneSpec();
	purgeGraveyard();
	if (display_)
		obs_display_add_draw_callback(display_, &RenderSurface::draw, this);
}

size_t RenderSurface::purgeGraveyard()
{
	graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
					[](const std::shared_ptr<RenderSnapshot> &s) { return s.use_count() == 1; }),
			 graveyard_.end());
	for (auto it = labelCache_.begin(); it != labelCache_.end();) {
		if (it->second.expired())
			it = labelCache_.erase(it);
		else
			++it;
	}
	return graveyard_.size();
}

static QColor qcolor(uint32_t rgb)
{
	return QColor::fromRgba(rgb);
}

std::shared_ptr<LabelTexture> RenderSurface::labelFor(const PaneSpec &spec, int pane, int w, int h)
{
	if (w < 4 || h < 4)
		return nullptr;
	char key[64];
	snprintf(key, sizeof(key), "%d|%dx%d|%d%d%d|%08x|", pane, w, h, spec.preview, spec.live, spec.dimmed,
		 spec.statusColor);
	std::string k = std::string(key) + spec.title.toStdString() + "|" + spec.status.toStdString();
	auto it = labelCache_.find(k);
	if (it != labelCache_.end()) {
		if (auto sp = it->second.lock())
			return sp;
	}

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(spec.dimmed ? QColor(28, 28, 28) : QColor(38, 38, 38));
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	QFont f = p.font();
	f.setPixelSize(std::max(8, (int)(h * 0.58)));
	f.setBold(true);
	p.setFont(f);
	QFontMetrics fm(f);
	const int pad = std::max(3, h / 5);

	// Badges on the right: text + colour, so tally never relies on colour alone.
	int right = w - pad;
	auto badge = [&](const QString &text, QColor bg) {
		int bw = fm.horizontalAdvance(text) + pad * 2;
		QRect r(right - bw, 2, bw, h - 4);
		QPainterPath path;
		path.addRoundedRect(r, 3, 3);
		p.fillPath(path, bg);
		p.setPen(Qt::white);
		p.drawText(r, Qt::AlignCenter, text);
		right -= bw + pad;
	};
	if (spec.live)
		badge("LIVE", QColor(200, 20, 20));
	if (spec.preview)
		badge("PREVIEW", QColor(20, 150, 40));

	if (!spec.status.isEmpty()) {
		QFont sf = f;
		sf.setBold(false);
		p.setFont(sf);
		QFontMetrics sfm(sf);
		QString st = sfm.elidedText(spec.status, Qt::ElideMiddle, std::max(0, (right - pad) / 2));
		int sw = sfm.horizontalAdvance(st);
		p.setPen(qcolor(spec.statusColor));
		p.drawText(QRect(right - sw, 0, sw, h), Qt::AlignVCenter | Qt::AlignRight, st);
		right -= sw + pad;
		p.setFont(f);
	}
	p.setPen(spec.dimmed ? QColor(140, 140, 140) : QColor(240, 240, 240));
	QString title = fm.elidedText(spec.title, Qt::ElideRight, std::max(0, right - pad));
	p.drawText(QRect(pad, 0, std::max(0, right - pad), h), Qt::AlignVCenter | Qt::AlignLeft, title);
	p.end();

	auto tex = std::make_shared<LabelTexture>(img);
	if (!tex->texture())
		return nullptr;
	labelCache_[k] = tex;
	return tex;
}

void RenderSurface::rebuild()
{
	if (!isVisible())
		return;
	auto snap = std::make_shared<RenderSnapshot>();
	const qreal dpr = devicePixelRatioF();
	QSize ps = physicalSize();
	canvasSize(snap->canvasW, snap->canvasH);

	LayoutInput in;
	in.width = ps.width();
	in.height = ps.height();
	in.canvasAspect = snap->canvasH ? (double)snap->canvasW / snap->canvasH : 16.0 / 9.0;
	in.spacing = 6 * dpr;
	in.labelHeight = std::round(22 * dpr);
	snap->layout = computeLayout(in);

	LayoutInput lin = in;
	lin.width = width();
	lin.height = height();
	lin.spacing = 6;
	lin.labelHeight = 22;
	logicalLayout_ = computeLayout(lin);

	std::unordered_set<obs_source_t *> unique;
	for (int i = 0; i < kPaneCount; i++) {
		const PaneSpec &spec = panes_[i];
		if (spec.source) {
			snap->sources[i] = obs_source_get_ref(spec.source);
			if (snap->sources[i])
				unique.insert(snap->sources[i]);
		}
		snap->renderProgram[i] = spec.renderProgram;
		snap->preview[i] = spec.preview;
		snap->live[i] = spec.live;
		snap->dimmed[i] = spec.dimmed;
		if (snap->layout.valid) {
			const Rect &lr = snap->layout.panes[i].label;
			snap->labels[i] = labelFor(spec, i, (int)lr.w, (int)lr.h);
		}
	}
	// Keep every displayed scene "showing" while the multiview is visible, so idle
	// cameras stay warm for a cut. Showing is not activation: no audio is mixed.
	for (obs_source_t *s : unique) {
		obs_source_inc_showing(s);
		snap->showing.push_back(s);
	}

	std::shared_ptr<RenderSnapshot> old;
	{
		std::lock_guard<std::mutex> lock(snapMutex_);
		old = std::move(snapshot_);
		snapshot_ = std::move(snap);
	}
	if (old)
		graveyard_.push_back(std::move(old));
	purgeGraveyard();
}

// ---------------------------------------------------------------------------
// graphics thread

void RenderSurface::draw(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<RenderSurface *>(data)->drawInternal(cx, cy);
}

static void drawRect(gs_effect_t *solid, gs_eparam_t *color, double x, double y, double w, double h, uint32_t abgr)
{
	if (w <= 0 || h <= 0)
		return;
	vec4 c;
	vec4_from_rgba(&c, abgr);
	gs_effect_set_vec4(color, &c);
	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate3f((float)x, (float)y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, (uint32_t)std::max(1.0, w), (uint32_t)std::max(1.0, h));
	gs_matrix_pop();
}

static void drawFrame(gs_effect_t *solid, gs_eparam_t *color, const Rect &r, double t, uint32_t abgr)
{
	drawRect(solid, color, r.x - t, r.y - t, r.w + 2 * t, t, abgr);
	drawRect(solid, color, r.x - t, r.y + r.h, r.w + 2 * t, t, abgr);
	drawRect(solid, color, r.x - t, r.y, t, r.h, abgr);
	drawRect(solid, color, r.x + r.w, r.y, t, r.h, abgr);
}

static void renderInViewport(const Rect &r, uint32_t baseW, uint32_t baseH, obs_source_t *source)
{
	if (r.w < 1 || r.h < 1 || !baseW || !baseH)
		return;
	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport((int)r.x, (int)r.y, (int)r.w, (int)r.h);
	gs_ortho(0.0f, (float)baseW, 0.0f, (float)baseH, -100.0f, 100.0f);
	if (source) {
		CleanFeedScope clean; // tiles and Preview: camera graphics are drawn on Program only
		obs_source_video_render(source);
	} else
		obs_render_main_texture();
	gs_projection_pop();
	gs_viewport_pop();
}

void RenderSurface::drawInternal(uint32_t cx, uint32_t cy)
{
	std::shared_ptr<RenderSnapshot> snap;
	{
		std::lock_guard<std::mutex> lock(snapMutex_);
		snap = snapshot_;
	}
	if (!snap || !snap->layout.valid)
		return;

	// The layout was computed for the widget size; scale if the swap chain differs (mid-resize).
	const double sx = snap->layout.width > 0 ? cx / snap->layout.width : 1.0;
	const double sy = snap->layout.height > 0 ? cy / snap->layout.height : 1.0;
	auto scaled = [&](const Rect &r) {
		return Rect{r.x * sx, r.y * sy, r.w * sx, r.h * sy};
	};

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_t *defEffect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(defEffect, "image");

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(0, 0, (int)cx, (int)cy);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	const double border = std::max(2.0, 3.0 * sy * (snap->layout.height / 1000.0 + 0.5));

	for (int i = 0; i < kPaneCount; i++) {
		const PaneGeometry &g = snap->layout.panes[i];
		const Rect area = scaled(g.video);
		drawRect(solid, color, area.x, area.y, area.w, area.h, 0xFF000000);

		if (snap->renderProgram[i]) {
			obs_video_info ovi = {};
			if (obs_get_video_info(&ovi)) {
				Rect fit = fitAspect(area, (double)ovi.base_width / ovi.base_height);
				renderInViewport(fit, ovi.base_width, ovi.base_height, nullptr);
			}
		} else if (obs_source_t *src = snap->sources[i]) {
			uint32_t bw = obs_source_get_width(src), bh = obs_source_get_height(src);
			if (bw && bh) {
				Rect fit = fitAspect(area, (double)bw / bh);
				renderInViewport(fit, bw, bh, src);
			}
		}

		// Restore the full-surface projection for overlays.
		gs_set_viewport(0, 0, (int)cx, (int)cy);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		if (snap->dimmed[i])
			drawRect(solid, color, area.x, area.y, area.w, area.h, 0xB0101010);

		// Tally: red outer frame for Program, green (inner when both) for Preview.
		const bool isTop = i == kPanePreview || i == kPaneProgram;
		if (snap->live[i] || i == kPaneProgram)
			drawFrame(solid, color, area, border, 0xFF1414D0);
		if (snap->preview[i] || i == kPanePreview) {
			Rect inner = (snap->live[i] && !isTop) ? Rect{area.x + border, area.y + border,
								      area.w - 2 * border, area.h - 2 * border}
							       : area;
			drawFrame(solid, color, inner, border, 0xFF28B414);
		}

		if (auto &lt = snap->labels[i]) {
			const Rect lr = scaled(g.label);
			gs_effect_set_texture(image, lt->texture());
			gs_matrix_push();
			gs_matrix_identity();
			gs_matrix_translate3f((float)lr.x, (float)lr.y, 0.0f);
			while (gs_effect_loop(defEffect, "Draw"))
				gs_draw_sprite(lt->texture(), 0, (uint32_t)lr.w, (uint32_t)lr.h);
			gs_matrix_pop();
		}
	}

	gs_projection_pop();
	gs_viewport_pop();
	framesDrawn_++;
}

} // namespace mf
