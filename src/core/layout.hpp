// MatchFrame Multiview - pure pane geometry (no OBS / Qt dependencies).
#pragma once

#include <array>

namespace mf {

struct Rect {
	double x = 0, y = 0, w = 0, h = 0;
	bool contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	bool empty() const { return w <= 0 || h <= 0; }
};

// Pane order is fixed and shared by renderer, hit testing and controllers.
enum PaneIndex : int {
	kPanePreview = 0,
	kPaneProgram = 1,
	kPaneCam1 = 2, // cameras 1..8 occupy 2..9
	kPaneReplay = 10,
	kPaneHighlights = 11,
	kPaneIntermission = 12,
	kPaneAds = 13,
	kPaneCount = 14,
};

inline int paneForCamera(int camIndex0)
{
	return kPaneCam1 + camIndex0;
}
inline bool paneIsCamera(int pane)
{
	return pane >= kPaneCam1 && pane < kPaneCam1 + 8;
}

struct PaneGeometry {
	Rect outer; // video + label strip; used for hit testing
	Rect video; // exact canvas-aspect area for the pane
	Rect label; // label strip below the video area
};

enum class LayoutMode {
	Auto,    // pick the arrangement that gives the largest monitors for the window size
	Stacked, // monitors / cameras 1-4 / cameras 5-8 / compact Replay, Highlights, Intermission, Ads row
	Wide,    // monitors / cameras 1-4 + Replay + Intermission / cameras 5-8 + Highlights + Ads
};

struct LayoutInput {
	double width = 0; // surface size, in the unit the caller wants back (physical px for rendering)
	double height = 0;
	double canvasAspect = 16.0 / 9.0;
	double spacing = 8;          // gap between panes and around the edge
	double labelHeight = 22;     // label strip height under each pane
	double playbackScale = 0.75; // playback row tiles relative to camera tiles ("compact row")
	LayoutMode mode = LayoutMode::Auto;
};

struct Layout {
	std::array<PaneGeometry, kPaneCount> panes{};
	double width = 0, height = 0;
	bool valid = false;
};

// Computes the 14-pane multiview layout with [Preview | Program] on top and the twelve
// scene tiles below, either stacked (two camera rows + a compact row of Replay,
// Highlights, Intermission and Video Ads) or wide (those four at the ends of the camera
// rows). Auto picks whichever fits the window best. All fourteen panes are always laid
// out (they shrink rather than disappear).
Layout computeLayout(const LayoutInput &in);

// Returns the pane index under (x, y) or -1. Coordinates use the same unit as the layout.
int hitTest(const Layout &layout, double x, double y);

// Largest rect of the given aspect ratio centred inside `area` (letterbox / pillarbox).
Rect fitAspect(const Rect &area, double aspect);

// Converts a logical (device-independent) point to the layout's physical unit.
inline void logicalToPhysical(double dpr, double lx, double ly, double &px, double &py)
{
	px = lx * dpr;
	py = ly * dpr;
}

} // namespace mf
