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

Layout computeLayout(const LayoutInput &in)
{
	Layout out;
	out.width = in.width;
	out.height = in.height;

	const double a = in.canvasAspect > 0 ? in.canvasAspect : 16.0 / 9.0;
	const double s = std::max(0.0, in.spacing);
	const double L = std::max(0.0, in.labelHeight);
	const double pbScale = std::clamp(in.playbackScale, 0.25, 1.0);

	if (!(in.width > 0) || !(in.height > 0))
		return out;

	// Width-limited sizes: two monitors across, four cameras across.
	double topW = (in.width - 3 * s) / 2.0;
	double camW = (in.width - 5 * s) / 4.0;
	double pbW = camW * pbScale;

	// Height needed: 4 rows of labels, 5 gaps, video heights.
	const double fixedH = 4 * L + 5 * s;
	const double videoH = (topW + 2 * camW + pbW) / a;
	if (fixedH + videoH > in.height) {
		// Shrink every row by the same factor so all twelve panes remain visible.
		// Preview/Program stay twice the camera width (priority to the large monitors).
		double k = (in.height - fixedH) / videoH;
		k = std::max(k, 0.0);
		topW *= k;
		camW *= k;
		pbW *= k;
	}
	if (topW < 1 || camW < 1 || pbW < 1)
		return out; // too small to draw anything meaningful

	const double topH = topW / a, camH = camW / a, pbH = pbW / a;
	const double usedH = topH + 2 * camH + pbH + fixedH;
	double y = s + std::max(0.0, (in.height - usedH) / 2.0);

	const int top[] = {kPanePreview, kPaneProgram};
	placeRow(out, top, 2, topW, topH, L, s, in.width, y);
	y += topH + L + s;

	const int row1[] = {kPaneCam1 + 0, kPaneCam1 + 1, kPaneCam1 + 2, kPaneCam1 + 3};
	placeRow(out, row1, 4, camW, camH, L, s, in.width, y);
	y += camH + L + s;

	const int row2[] = {kPaneCam1 + 4, kPaneCam1 + 5, kPaneCam1 + 6, kPaneCam1 + 7};
	placeRow(out, row2, 4, camW, camH, L, s, in.width, y);
	y += camH + L + s;

	const int pb[] = {kPaneReplay, kPaneHighlights};
	placeRow(out, pb, 2, pbW, pbH, L, s, in.width, y);

	out.valid = true;
	return out;
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
