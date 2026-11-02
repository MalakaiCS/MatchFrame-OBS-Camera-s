#include "layout.hpp"

#include <algorithm>
#include <cmath>

namespace mf {

Rect fitAspect(const Rect &area, double aspect)
{
	if (area.empty() || !(aspect > 0))
		return Rect{area.x, area.y, 0, 0};
	double w = area.w;
	double h = w / aspect;
	if (h > area.h) {
		h = area.h;
		w = h * aspect;
	}
	return Rect{area.x + (area.w - w) / 2.0, area.y + (area.h - h) / 2.0, w, h};
}

static void placeRow(Layout &out, const int *panes, int count, double tileW, double tileH, double label, double spacing,
		     double totalWidth, double y)
{
	const double rowW = count * tileW + (count - 1) * spacing;
	double x = std::max(spacing, (totalWidth - rowW) / 2.0);
	for (int i = 0; i < count; i++) {
		PaneGeometry &g = out.panes[panes[i]];
		g.video = Rect{x, y, tileW, tileH};
		g.label = Rect{x, y + tileH, tileW, label};
		g.outer = Rect{x, y, tileW, tileH + label};
		x += tileW + spacing;
	}
}

// Shrinks the rows uniformly when the stacked video heights do not fit.
static double fitFactor(double available, double fixedH, double videoH)
{
	if (fixedH + videoH <= available)
		return 1.0;
	return std::max(0.0, (available - fixedH) / videoH);
}

static int ceilDiv(int a, int b)
{
	return (a + b - 1) / b;
}

static void place(Layout &out, int pane, double x, double y, double w, double h, double label)
{
	PaneGeometry &g = out.panes[pane];
	g.video = Rect{x, y, w, h};
	g.label = Rect{x, y + h, w, label};
	g.outer = Rect{x, y, w, h + label};
}

// Stacked: [Preview | Program] / camera rows of `perRow` / compact
// [Replay | Highlights | Intermission | Video Ads]. Best for tall or narrow windows.
static Layout stackedLayout(const LayoutInput &in, int cams, int perRow, double a, double s, double L)
{
	Layout out;
	out.width = in.width;
	out.height = in.height;
	const double pbScale = std::clamp(in.playbackScale, 0.25, 1.0);
	const int rows = ceilDiv(cams, perRow);

	double topW = (in.width - 3 * s) / 2.0;
	double camW = (in.width - (perRow + 1) * s) / perRow;
	double pbW = std::min(camW * pbScale, (in.width - 5 * s) / 4.0);
	const double fixedH = (rows + 2) * L + (rows + 3) * s;
	const double k = fitFactor(in.height, fixedH, (topW + rows * camW + pbW) / a);
	topW *= k;
	camW *= k;
	pbW *= k;
	if (topW < 1 || camW < 1 || pbW < 1)
		return out;

	const double topH = topW / a, camH = camW / a, pbH = pbW / a;
	const double usedH = topH + rows * camH + pbH + fixedH;
	double y = s + std::max(0.0, (in.height - usedH) / 2.0);

	const int top[] = {kPanePreview, kPaneProgram};
	placeRow(out, top, 2, topW, topH, L, s, in.width, y);
	y += topH + L + s;
	for (int r = 0; r < rows; r++) {
		int row[16];
		int n = 0;
		for (int c = r * perRow; c < std::min(cams, (r + 1) * perRow); c++)
			row[n++] = kPaneCam1 + c;
		placeRow(out, row, n, camW, camH, L, s, in.width, y);
		y += camH + L + s;
	}
	const int pb[] = {kPaneReplay, kPaneHighlights, kPaneIntermission, kPaneAds};
	placeRow(out, pb, 4, pbW, pbH, L, s, in.width, y);

	out.valid = true;
	return out;
}

// Wide: [Preview | Program] / `rows` camera rows, with Replay + Intermission ending row 1
// and Highlights + Video Ads ending row 2 (two extra columns on the right). Fewer rows than
// stacked, so on 16:9 and wider screens the monitors get larger.
static Layout wideLayout(const LayoutInput &in, int cams, int rows, double a, double s, double L)
{
	Layout out;
	out.width = in.width;
	out.height = in.height;
	const int perRow = ceilDiv(cams, rows);
	const int cols = perRow + 2;

	double topW = (in.width - 3 * s) / 2.0;
	double tileW = (in.width - (cols + 1) * s) / cols;
	const double fixedH = (rows + 1) * L + (rows + 2) * s;
	const double k = fitFactor(in.height, fixedH, (topW + rows * tileW) / a);
	topW *= k;
	tileW *= k;
	if (topW < 1 || tileW < 1)
		return out;

	const double topH = topW / a, tileH = tileW / a;
	const double usedH = topH + rows * tileH + fixedH;
	double y = s + std::max(0.0, (in.height - usedH) / 2.0);

	const int top[] = {kPanePreview, kPaneProgram};
	placeRow(out, top, 2, topW, topH, L, s, in.width, y);
	y += topH + L + s;
	// A fixed grid: cameras fill the left columns, the extras keep the last two columns.
	const double gridW = cols * tileW + (cols - 1) * s;
	const double x0 = std::max(s, (in.width - gridW) / 2.0);
	auto colX = [&](int c) {
		return x0 + c * (tileW + s);
	};
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < perRow; c++) {
			const int cam = r * perRow + c;
			if (cam < cams)
				place(out, kPaneCam1 + cam, colX(c), y, tileW, tileH, L);
		}
		if (r == 0) {
			place(out, kPaneReplay, colX(perRow), y, tileW, tileH, L);
			place(out, kPaneIntermission, colX(perRow + 1), y, tileW, tileH, L);
		} else if (r == 1) {
			place(out, kPaneHighlights, colX(perRow), y, tileW, tileH, L);
			place(out, kPaneAds, colX(perRow + 1), y, tileW, tileH, L);
		}
		y += tileH + L + s;
	}

	out.valid = true;
	return out;
}

// True when `a` is the better layout: larger monitors first, then larger camera tiles.
static bool better(const Layout &a, const Layout &b)
{
	if (!a.valid)
		return false;
	if (!b.valid)
		return true;
	const double at = a.panes[kPanePreview].video.w, bt = b.panes[kPanePreview].video.w;
	if (std::fabs(at - bt) > 1.0)
		return at > bt;
	return a.panes[kPaneCam1].video.w > b.panes[kPaneCam1].video.w + 1.0;
}

Layout computeLayout(const LayoutInput &in)
{
	Layout invalid;
	invalid.width = in.width;
	invalid.height = in.height;
	if (!(in.width > 0) || !(in.height > 0))
		return invalid;

	const double a = in.canvasAspect > 0 ? in.canvasAspect : 16.0 / 9.0;
	const double s = std::max(0.0, in.spacing);
	const double L = std::max(0.0, in.labelHeight);
	const int cams = std::clamp(in.cameraCount, 4, 16);

	// Candidate arrangements. Up to 8 cameras: four per stacked row, or two wide rows
	// (the original layouts). More cameras: 2, 3 or 4 rows in either arrangement.
	Layout best = invalid;
	auto consider = [&](const Layout &l) {
		if (better(l, best))
			best = l;
	};
	if (in.mode != LayoutMode::Wide) {
		if (cams <= 8) {
			consider(stackedLayout(in, cams, 4, a, s, L));
		} else {
			for (int rows : {2, 3, 4})
				consider(stackedLayout(in, cams, std::max(4, ceilDiv(cams, rows)), a, s, L));
		}
	}
	if (in.mode != LayoutMode::Stacked) {
		if (cams <= 8) {
			consider(wideLayout(in, cams, 2, a, s, L));
		} else {
			for (int rows : {2, 3, 4})
				consider(wideLayout(in, cams, rows, a, s, L));
		}
	}
	return best;
}

int hitTest(const Layout &layout, double x, double y)
{
	if (!layout.valid)
		return -1;
	for (int i = 0; i < kPaneCount; i++) {
		if (layout.panes[i].outer.contains(x, y))
			return i;
	}
	return -1;
}

} // namespace mf
