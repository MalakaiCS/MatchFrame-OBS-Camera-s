// MatchFrame Multiview - unit tests for the OBS-independent core.
// Minimal self-contained harness (no external test framework dependency).

#include "core/config.hpp"
#include "core/layout.hpp"
#include "core/playback-logic.hpp"
#include "core/replay-cue-status.hpp"
#include "core/switch-logic.hpp"

#include <obs-data.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace mf;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                   \
	do {                                                                          \
		g_checks++;                                                           \
		if (!(cond)) {                                                        \
			g_failures++;                                                 \
			std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                     \
	} while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

struct TestCase {
	const char *name;
	std::function<void()> fn;
};
static std::vector<TestCase> &tests()
{
	static std::vector<TestCase> t;
	return t;
}
struct Register {
	Register(const char *n, std::function<void()> f) { tests().push_back({n, std::move(f)}); }
};
#define TEST(name)                               \
	static void name();                      \
	static Register reg_##name(#name, name); \
	static void name()

// =============================================================================
// Layout

static bool rectInside(const Rect &inner, double w, double h)
{
	return inner.x >= -1e-6 && inner.y >= -1e-6 && inner.x + inner.w <= w + 1e-6 && inner.y + inner.h <= h + 1e-6;
}

static bool overlaps(const Rect &a, const Rect &b)
{
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void checkLayoutInvariants(double w, double h, double aspect, double dpr)
{
	LayoutInput in;
	in.width = w * dpr;
	in.height = h * dpr;
	in.canvasAspect = aspect;
	in.spacing = 6 * dpr;
	in.labelHeight = 22 * dpr;
	Layout l = computeLayout(in);
	CHECK(l.valid);
	if (!l.valid)
		return;
	for (int i = 0; i < kPaneCount; i++) {
		const PaneGeometry &g = l.panes[i];
		CHECK(rectInside(g.outer, in.width, in.height));
		CHECK(g.video.w > 0 && g.video.h > 0);
		// exact canvas aspect for every pane
		CHECK(std::fabs(g.video.w / g.video.h - aspect) < 1e-6);
		for (int j = i + 1; j < kPaneCount; j++)
			CHECK(!overlaps(g.outer, l.panes[j].outer));
	}
	// Priority: Preview/Program larger than cameras, cameras >= playback tiles.
	CHECK(l.panes[kPanePreview].video.w > l.panes[kPaneCam1].video.w);
	CHECK(l.panes[kPaneCam1].video.w >= l.panes[kPaneReplay].video.w);
	// Row order: monitors on top, cameras 1-4 above 5-8, playback below the monitors.
	CHECK(l.panes[kPanePreview].outer.y < l.panes[kPaneCam1].outer.y);
	CHECK(l.panes[kPaneCam1].outer.y < l.panes[kPaneCam1 + 4].outer.y);
	CHECK(l.panes[kPanePreview].outer.y < l.panes[kPaneReplay].outer.y);
	CHECK(l.panes[kPaneReplay].outer.y <= l.panes[kPaneHighlights].outer.y);
	// Program right of Preview, cameras ordered left to right.
	CHECK(l.panes[kPaneProgram].outer.x > l.panes[kPanePreview].outer.x);
	for (int c = 0; c < 3; c++) {
		CHECK(l.panes[kPaneCam1 + c].outer.x < l.panes[kPaneCam1 + c + 1].outer.x);
		CHECK(l.panes[kPaneCam1 + 4 + c].outer.x < l.panes[kPaneCam1 + 5 + c].outer.x);
	}
	// Hit testing: centre of every pane maps back to that pane, at this DPI.
	for (int i = 0; i < kPaneCount; i++) {
		const Rect &r = l.panes[i].outer;
		CHECK_EQ(hitTest(l, r.x + r.w / 2, r.y + r.h / 2), i);
		// logical -> physical conversion used by the widget
		double px, py;
		logicalToPhysical(dpr, (r.x + r.w / 2) / dpr, (r.y + r.h / 2) / dpr, px, py);
		CHECK_EQ(hitTest(l, px, py), i);
	}
	// Gaps do not hit anything.
	CHECK_EQ(hitTest(l, 0.5, 0.5), -1);
}

TEST(layout_1080p_and_variants)
{
	checkLayoutInvariants(1920, 960, 16.0 / 9.0, 1.0);
	checkLayoutInvariants(1920, 960, 16.0 / 9.0, 1.5);
	checkLayoutInvariants(1920, 960, 16.0 / 9.0, 2.0);
	checkLayoutInvariants(960, 560, 16.0 / 9.0, 1.0);   // minimum window
	checkLayoutInvariants(3840, 2000, 16.0 / 9.0, 1.0); // 4K
	checkLayoutInvariants(1280, 1400, 16.0 / 9.0, 1.0); // portrait-ish window
	checkLayoutInvariants(1920, 960, 4.0 / 3.0, 1.0);   // 4:3 canvas
	checkLayoutInvariants(2560, 1000, 21.0 / 9.0, 1.25);
}

TEST(layout_uses_space_at_1080p)
{
	LayoutInput in;
	in.width = 1908;
	in.height = 940;
	Layout l = computeLayout(in);
	CHECK(l.valid);
	// The large monitors must be meaningfully large on a 1080p screen.
	CHECK(l.panes[kPanePreview].video.w >= 550);
	// Height is used: the lowest row ends near the bottom edge.
	double bottom = 0;
	for (const auto &g : l.panes)
		bottom = std::max(bottom, g.outer.y + g.outer.h);
	CHECK(bottom <= in.height);
	CHECK(bottom >= in.height - 3 * in.spacing - 1);
}

TEST(layout_auto_fills_wide_screens)
{
	// The operator's case: a 24.5" 1080p monitor, multiview window ~1915 x 850 surface.
	LayoutInput in;
	in.width = 1915;
	in.height = 850;
	Layout autoL = computeLayout(in);
	in.mode = LayoutMode::Stacked;
	Layout stacked = computeLayout(in);
	in.mode = LayoutMode::Wide;
	Layout wide = computeLayout(in);
	CHECK(autoL.valid && stacked.valid && wide.valid);
	// Wide uses one row fewer, so every pane is larger on a 16:9 screen; Auto picks it.
	CHECK(wide.panes[kPanePreview].video.w > stacked.panes[kPanePreview].video.w * 1.2);
	CHECK(autoL.panes[kPanePreview].video.w == wide.panes[kPanePreview].video.w);
	CHECK(autoL.panes[kPanePreview].video.w >= 700);
	// Replay ends camera row 1, Highlights ends camera row 2.
	CHECK(autoL.panes[kPaneReplay].outer.y == autoL.panes[kPaneCam1].outer.y);
	CHECK(autoL.panes[kPaneHighlights].outer.y == autoL.panes[kPaneCam1 + 4].outer.y);
	CHECK(autoL.panes[kPaneReplay].outer.x > autoL.panes[kPaneCam1 + 3].outer.x);
	// Full screen on the same monitor gets bigger still.
	in.mode = LayoutMode::Auto;
	in.height = 1000;
	CHECK(computeLayout(in).panes[kPanePreview].video.w > autoL.panes[kPanePreview].video.w);
}

TEST(layout_auto_keeps_stacked_for_tall_windows)
{
	LayoutInput in;
	in.width = 1000;
	in.height = 1400; // portrait / narrow: monitors are width-limited either way
	Layout l = computeLayout(in);
	CHECK(l.valid);
	CHECK(l.panes[kPaneReplay].outer.y > l.panes[kPaneCam1 + 4].outer.y);  // compact bottom row
	CHECK(l.panes[kPaneCam1].video.w > (in.width - 6 * in.spacing) / 5.0); // 4-across cameras
}

TEST(layout_modes_all_valid)
{
	for (LayoutMode m : {LayoutMode::Auto, LayoutMode::Stacked, LayoutMode::Wide}) {
		LayoutInput in;
		in.mode = m;
		for (double w : {960.0, 1280.0, 1920.0, 2560.0, 3840.0})
			for (double h : {560.0, 720.0, 1000.0, 1400.0, 2100.0}) {
				in.width = w;
				in.height = h;
				Layout l = computeLayout(in);
				CHECK(l.valid);
				for (int i = 0; i < kPaneCount; i++) {
					CHECK(rectInside(l.panes[i].outer, w, h));
					for (int j = i + 1; j < kPaneCount; j++)
						CHECK(!overlaps(l.panes[i].outer, l.panes[j].outer));
					CHECK_EQ(hitTest(l, l.panes[i].outer.x + 1, l.panes[i].outer.y + 1), i);
				}
			}
	}
}

TEST(layout_small_and_degenerate)
{
	LayoutInput in;
	in.width = 0;
	in.height = 100;
	CHECK(!computeLayout(in).valid);
	in.width = 40;
	in.height = 30;
	CHECK(!computeLayout(in).valid); // too small: reports invalid instead of overlapping garbage
	CHECK_EQ(hitTest(computeLayout(in), 1, 1), -1);
}

TEST(fit_aspect_letterbox)
{
	Rect r = fitAspect(Rect{0, 0, 400, 400}, 16.0 / 9.0);
	CHECK(std::fabs(r.w - 400) < 1e-9);
	CHECK(std::fabs(r.h - 225) < 1e-9);
	CHECK(std::fabs(r.y - 87.5) < 1e-9);
	Rect p = fitAspect(Rect{10, 10, 400, 100}, 16.0 / 9.0);
	CHECK(std::fabs(p.h - 100) < 1e-9);
	CHECK(std::fabs(p.x - (10 + (400 - 177.7777777) / 2)) < 1e-3);
	CHECK(fitAspect(Rect{0, 0, 0, 10}, 1.0).w == 0);
}

// =============================================================================
// Switching

static SlotFacts camOk()
{
	SlotFacts f;
	f.sceneExists = f.inputAssigned = f.inputExists = true;
	return f;
}

TEST(select_rules)
{
	SelectFacts f;
	f.studioMode = true;
	f.slot = 0;
	f.slotFacts = camOk();
	CHECK(evaluateSelect(f) == Reject::None);

	f.studioMode = false;
	CHECK(evaluateSelect(f) == Reject::StudioModeOff);
	f.studioMode = true;

	f.collectionChanging = true;
	CHECK(evaluateSelect(f) == Reject::CollectionChanging);
	f.collectionChanging = false;

	f.slotFacts.inputAssigned = false;
	CHECK(evaluateSelect(f) == Reject::Unassigned);
	f.slotFacts = camOk();
	f.slotFacts.inputExists = false;
	CHECK(evaluateSelect(f) == Reject::InputMissing); // deleted input: slot visible but not selectable
	f.slotFacts = camOk();
	f.slotFacts.sceneExists = false;
	CHECK(evaluateSelect(f) == Reject::SceneMissing);

	// Playback: must be READY, except selecting the scene already on Program is allowed.
	f.slot = kSlotReplay;
	f.slotFacts = SlotFacts();
	f.slotFacts.sceneExists = true;
	CHECK(evaluateSelect(f) == Reject::PlaybackNotReady);
	f.slotFacts.playbackReady = true;
	CHECK(evaluateSelect(f) == Reject::None);
	f.slotFacts.playbackReady = false;
	f.slotFacts.playbackOnAir = true;
	CHECK(evaluateSelect(f) == Reject::None);

	f.slot = 42;
	CHECK(evaluateSelect(f) == Reject::SceneMissing);
}

static CutFacts cutOk()
{
	CutFacts f;
	f.studioMode = true;
	f.cutTransitionAvailable = true;
	f.previewExists = true;
	f.previewSlot = 2;
	f.previewSlotFacts = camOk();
	return f;
}

TEST(cut_rules)
{
	CutFacts f = cutOk();
	CHECK(evaluateCut(f).action == CutAction::Execute);

	f.previewEqualsProgram = true;
	CHECK(evaluateCut(f).action == CutAction::NoOp); // never restarts media

	f = cutOk();
	f.busy = true;
	CHECK(evaluateCut(f).reason == Reject::Busy);
	f = cutOk();
	f.transitionActive = true;
	CHECK(evaluateCut(f).reason == Reject::TransitionActive);
	f = cutOk();
	f.studioMode = false;
	CHECK(evaluateCut(f).reason == Reject::StudioModeOff);
	f = cutOk();
	f.previewOverrideConflict = true;
	CHECK(evaluateCut(f).reason == Reject::TransitionOverride);
	f = cutOk();
	f.cutTransitionAvailable = false;
	CHECK(evaluateCut(f).reason == Reject::NoCutTransition);
	f = cutOk();
	f.previewExists = false;
	CHECK(evaluateCut(f).reason == Reject::NoPreview);

	// Playback preview: must be READY for a plugin take (on-air exemption does not apply to takes).
	f = cutOk();
	f.previewSlot = kSlotHighlights;
	f.previewSlotFacts = SlotFacts();
	f.previewSlotFacts.sceneExists = true;
	CHECK(evaluateCut(f).reason == Reject::PlaybackNotReady);
	f.previewSlotFacts.playbackReady = true;
	CHECK(evaluateCut(f).action == CutAction::Execute);

	// A scene outside the bank is cut as-is (it is the real OBS Preview).
	f = cutOk();
	f.previewSlot = -1;
	f.previewSlotFacts = SlotFacts();
	CHECK(evaluateCut(f).action == CutAction::Execute);

	// Invalid camera in preview (input deleted after selection) is rejected.
	f = cutOk();
	f.previewSlotFacts.inputExists = false;
	CHECK(evaluateCut(f).reason == Reject::InputMissing);
}

TEST(take_state_machine_suppresses_repeats)
{
	TakeStateMachine t(2000);
	CHECK(t.state() == TakeStateMachine::State::Idle);
	CHECK(t.tryBegin(1000));
	CHECK(t.busy());
	// key-repeat / double-click storm while taking: dropped, not queued
	for (int i = 0; i < 25; i++)
		CHECK(!t.tryBegin(1000 + i));
	CHECK_EQ(t.dropped(), 25u);
	t.complete();
	CHECK(!t.busy());
	CHECK(t.tryBegin(1500));
	// no completion: released after timeout
	CHECK(!t.tick(2000));
	CHECK(t.busy());
	CHECK(t.tick(3600));
	CHECK(!t.busy());
	// disabled during collection change
	t.setDisabled(true);
	CHECK(!t.tryBegin(4000));
	t.setDisabled(false);
	CHECK(t.tryBegin(4000));
	// complete() in Idle is harmless
	t.complete();
	t.complete();
	CHECK(t.state() == TakeStateMachine::State::Idle);
}

TEST(last_camera_tracker)
{
	LastCameraTracker t;
	CHECK(t.lastCameraId().empty());
	t.observeProgram(2, "cam3");
	CHECK_EQ(t.lastCameraId(), std::string("cam3"));
	CHECK_EQ(t.lastCameraSlot(), 2);
	// Replay then Highlights: identity preserved across playback changes.
	t.observeProgram(kSlotReplay, "replay");
	t.observeProgram(kSlotHighlights, "hl");
	t.observeProgram(kSlotReplay, "replay");
	CHECK_EQ(t.lastCameraId(), std::string("cam3"));
	// External scene on Program does not count as a camera.
	t.observeProgram(-1, "someOtherScene");
	CHECK_EQ(t.lastCameraId(), std::string("cam3"));
	t.observeProgram(6, "cam7");
	CHECK_EQ(t.lastCameraId(), std::string("cam7"));
	t.observeProgram(0, "");
	CHECK_EQ(t.lastCameraId(), std::string("cam7"));
	t.reset("cam1", 0);
	CHECK_EQ(t.lastCameraSlot(), 0);
	t.reset();
	CHECK_EQ(t.lastCameraSlot(), -1);
}

TEST(return_live_rules)
{
	ReturnLiveFacts f;
	f.studioMode = true;
	f.cutTransitionAvailable = true;
	f.targetKnown = f.targetExists = f.targetInputExists = true;
	CHECK(evaluateReturnLive(f).action == CutAction::Execute);
	ReturnLiveFacts g = f;
	g.targetExists = false; // deleted camera scene: disabled, never a random replacement
	CHECK(evaluateReturnLive(g).reason == Reject::NoTarget);
	g = f;
	g.targetKnown = false;
	CHECK(evaluateReturnLive(g).reason == Reject::NoTarget);
	g = f;
	g.targetInputExists = false;
	CHECK(evaluateReturnLive(g).reason == Reject::InputMissing);
	g = f;
	g.targetOnProgram = true;
	CHECK(evaluateReturnLive(g).reason == Reject::AlreadyOnProgram);
	g = f;
	g.busy = true;
	CHECK(evaluateReturnLive(g).reason == Reject::Busy);
	g = f;
	g.targetOverrideConflict = true;
	CHECK(evaluateReturnLive(g).reason == Reject::TransitionOverride);
	CHECK(std::string(rejectMessage(Reject::NoTarget)).size() > 5);
}

// =============================================================================
// Playback

static MediaObservation obs(MediaState s, int64_t dur, int64_t t, bool frame)
{
	MediaObservation o;
	o.state = s;
	o.durationMs = dur;
	o.timeMs = t;
	o.hasFrame = frame;
	return o;
}

// Drives a slot from load to READY the way the controller does.
static void armToReady(PlaybackSlotLogic &l, int64_t &now)
{
	CHECK(l.requestLoad("/clips/replay.mp4", true, false, false, now) == PlaybackAction::SetFileAndOpen);
	CHECK(l.state() == PlaybackState::Loading);
	now += 50;
	CHECK(l.observe(obs(MediaState::None, 0, 0, false), now) == PlaybackAction::None);
	now += 50;
	CHECK(l.observe(obs(MediaState::Paused, 12000, 40, true), now) == PlaybackAction::PauseSeekStart);
	now += 100;
	CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), now) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Loading); // settle time not elapsed
	now += 150;
	CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), now) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Ready);
}

TEST(playback_arm_ready)
{
	PlaybackSlotLogic l;
	int64_t now = 10000;
	armToReady(l, now);
	CHECK(l.armed());
	// Staying in Preview / thumbnail for a long time: repeated observations never act.
	for (int i = 0; i < 600; i++) {
		now += 100;
		CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), now) == PlaybackAction::None);
	}
	CHECK(l.state() == PlaybackState::Ready);
}

TEST(playback_program_entry_starts_once)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	CHECK(l.onProgramEnter(now) == PlaybackAction::Restart);
	CHECK(l.state() == PlaybackState::Playing);
	// Re-observing Program (resize, preview change, thumbnail refresh, repeated poll) never restarts.
	for (int i = 0; i < 50; i++) {
		CHECK(l.onProgramEnter(now + i) == PlaybackAction::None);
		CHECK(l.observe(obs(MediaState::Playing, 12000, 100 * i, true), now + i) == PlaybackAction::None);
	}
	// Ends: ENDED shown, no automatic action.
	CHECK(l.observe(obs(MediaState::Ended, 12000, 12000, true), now + 100) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Ended);
	// Leaving Program: re-arm from the beginning.
	CHECK(l.onProgramExit(false, now + 200) == PlaybackAction::PauseSeekStart);
	CHECK(l.state() == PlaybackState::Loading);
	CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), now + 300) == PlaybackAction::None);
	CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), now + 600) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Ready);
}

TEST(playback_native_take_and_live_states)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	// Native OBS take (bypassing the plugin) is handled identically.
	CHECK(l.onProgramEnter(now) == PlaybackAction::Restart);
	CHECK(l.observe(obs(MediaState::Playing, 12000, 30, true), now + 10) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Playing);
	// Operator pauses the live clip in OBS: reported truthfully.
	CHECK(l.observe(obs(MediaState::Paused, 12000, 3000, true), now + 3000) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Paused);
	CHECK(l.observe(obs(MediaState::Playing, 12000, 3100, true), now + 3100) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Playing);
}

TEST(playback_restart_grace_on_program)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	CHECK(l.onProgramEnter(1000) == PlaybackAction::Restart);
	// OBS applies the restart on its next tick: the source still reports PAUSED briefly.
	CHECK(l.observe(obs(MediaState::Paused, 12000, 0, true), 1010) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Playing); // no PLAYING -> PAUSED flicker
	CHECK(l.observe(obs(MediaState::Playing, 12000, 20, true), 1300) == PlaybackAction::None);
	// Once confirmed, a real pause is reported truthfully.
	l.observe(obs(MediaState::Paused, 12000, 500, true), 1400);
	CHECK(l.state() == PlaybackState::Paused);
	// If OBS never confirms, the grace period ends and the real state is shown.
	PlaybackSlotLogic m;
	now = 0;
	armToReady(m, now);
	m.onProgramEnter(5000);
	m.observe(obs(MediaState::Paused, 12000, 0, true), 6000);
	CHECK(m.state() == PlaybackState::Playing);
	m.observe(obs(MediaState::Paused, 12000, 0, true), 6600);
	CHECK(m.state() == PlaybackState::Paused);
}

TEST(playback_take_of_unready_slot_starts_if_loaded)
{
	// Native take while still arming: it is on air, so start from the beginning.
	PlaybackSlotLogic l;
	CHECK(l.requestLoad("/a.mp4", true, false, false, 0) == PlaybackAction::SetFileAndOpen);
	CHECK(l.onProgramEnter(10) == PlaybackAction::Restart);
	// Empty slot: nothing to play.
	PlaybackSlotLogic e;
	CHECK(e.onProgramEnter(0) == PlaybackAction::None);
	CHECK(e.state() == PlaybackState::Empty);
}

TEST(playback_modification_blocked_on_air)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	l.onProgramEnter(now);
	std::string why;
	CHECK(l.requestLoad("/b.mp4", true, true, true, now, &why) == PlaybackAction::None);
	CHECK(!why.empty());
	CHECK_EQ(l.file(), std::string("/clips/replay.mp4")); // unchanged
	why.clear();
	CHECK(l.requestRearm(true, true, now, &why) == PlaybackAction::None);
	CHECK(!why.empty());
	// Active elsewhere in the output (not via this scene): also blocked.
	PlaybackSlotLogic m;
	why.clear();
	CHECK(m.requestLoad("/c.mp4", true, false, true, 0, &why) == PlaybackAction::None);
	CHECK(why.find("elsewhere") != std::string::npos);
	// Transport is allowed while live (explicitly labelled LIVE in the UI).
	CHECK(l.transport(PlaybackAction::PlayPause) == PlaybackAction::PlayPause);
	CHECK(l.transport(PlaybackAction::Restart) == PlaybackAction::Restart);
	CHECK(l.transport(PlaybackAction::SetFileAndOpen) == PlaybackAction::None);
}

TEST(playback_exit_while_still_active)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	l.onProgramEnter(now);
	// Scene left Program but the source is still used in the output: do not stop it.
	CHECK(l.onProgramExit(true, now + 10) == PlaybackAction::None);
	CHECK(l.activeElsewhere());
	CHECK(l.onBecameInactive(now + 500) == PlaybackAction::PauseSeekStart);
	CHECK(!l.activeElsewhere());
	CHECK(l.state() == PlaybackState::Loading);
}

TEST(playback_errors_and_timeouts)
{
	PlaybackSlotLogic l;
	std::string why;
	CHECK(l.requestLoad("/missing.mp4", false, false, false, 0, &why) == PlaybackAction::None);
	CHECK(l.state() == PlaybackState::Error);
	CHECK(!why.empty());
	CHECK(l.onProgramEnter(1) == PlaybackAction::None); // an errored slot never "plays"

	PlaybackSlotLogic t;
	CHECK(t.requestLoad("/slow.mp4", true, false, false, 0) == PlaybackAction::SetFileAndOpen);
	CHECK(t.observe(obs(MediaState::None, 0, 0, false), 4000) == PlaybackAction::None);
	CHECK(t.state() == PlaybackState::Loading);
	t.observe(obs(MediaState::None, 0, 0, false), 9000);
	CHECK(t.state() == PlaybackState::Error);

	PlaybackSlotLogic e;
	e.requestLoad("/bad.mp4", true, false, false, 0);
	e.observe(obs(MediaState::Error, 0, 0, false), 100);
	CHECK(e.state() == PlaybackState::Error);

	// Pause never takes effect: bounded retries then error.
	PlaybackSlotLogic p;
	p.requestLoad("/x.mp4", true, false, false, 0);
	CHECK(p.observe(obs(MediaState::Playing, 5000, 10, true), 100) == PlaybackAction::PauseSeekStart);
	int repauses = 0;
	for (int64_t now = 200; now < 7000; now += 100)
		if (p.observe(obs(MediaState::Playing, 5000, now, true), now) == PlaybackAction::PauseSeekStart)
			repauses++;
	CHECK_EQ(repauses, 3);
	CHECK(p.state() == PlaybackState::Error);

	// Empty path
	PlaybackSlotLogic q;
	CHECK(q.requestLoad("", true, false, false, 0, &why) == PlaybackAction::None);
	CHECK(q.state() == PlaybackState::Empty);
}

TEST(playback_offair_transport_disarms)
{
	PlaybackSlotLogic l;
	int64_t now = 0;
	armToReady(l, now);
	CHECK(l.transport(PlaybackAction::PlayPause) == PlaybackAction::PlayPause);
	CHECK(l.state() != PlaybackState::Ready);
	// someone presses play inside OBS while off-air: truthful PLAYING, not READY
	PlaybackSlotLogic m;
	now = 0;
	armToReady(m, now);
	m.observe(obs(MediaState::Playing, 12000, 500, true), now + 100);
	CHECK(m.state() == PlaybackState::Playing);
	CHECK(!m.armed());
	std::string why;
	CHECK(m.requestRearm(false, false, now + 200, &why) == PlaybackAction::PauseSeekStart);
}

TEST(playback_independent_slots)
{
	PlaybackSlotLogic replay, highlights;
	int64_t now = 0;
	armToReady(replay, now);
	CHECK(replay.onProgramEnter(now) == PlaybackAction::Restart);
	// Loading a reel into Highlights while Replay is live is allowed and independent.
	CHECK(highlights.requestLoad("/ht.mp4", true, false, false, now) == PlaybackAction::SetFileAndOpen);
	CHECK(replay.state() == PlaybackState::Playing);
	CHECK_EQ(replay.file(), std::string("/clips/replay.mp4"));
	CHECK_EQ(highlights.file(), std::string("/ht.mp4"));
}

TEST(playback_adopt_existing)
{
	PlaybackSlotLogic l;
	l.adoptExisting("/a.mp4", true, false, 0);
	CHECK(l.state() == PlaybackState::Loading);
	CHECK(l.observe(obs(MediaState::Playing, 8000, 300, true), 100) == PlaybackAction::PauseSeekStart);
	l.observe(obs(MediaState::Paused, 8000, 0, true), 200);
	l.observe(obs(MediaState::Paused, 8000, 0, true), 450);
	CHECK(l.state() == PlaybackState::Ready);
	PlaybackSlotLogic live;
	live.adoptExisting("/a.mp4", true, true, 0);
	CHECK(live.state() == PlaybackState::Playing);
	CHECK(live.onProgram());
	PlaybackSlotLogic missing;
	missing.adoptExisting("/gone.mp4", false, false, 0);
	CHECK(missing.state() == PlaybackState::Error);
}

TEST(format_clock)
{
	CHECK_EQ(formatClock(0), std::string("0:00"));
	CHECK_EQ(formatClock(65400), std::string("1:05"));
	CHECK_EQ(formatClock(3725000), std::string("1:02:05"));
	CHECK_EQ(formatClock(-5), std::string("0:00"));
}

// =============================================================================
// Configuration

TEST(config_roundtrip)
{
	BankConfig c = makeDefaultConfig("bank123");
	c.cameras[0].sceneUuid = "uuid-s1";
	c.cameras[0].inputUuid = "uuid-i1";
	c.cameras[0].inputName = "Cam A";
	c.cameras[0].inputTypeId = "v4l2_input";
	c.cameras[0].inputItemId = 7;
	c.cameras[0].graphicsItemId = 9;
	c.cameras[3].label = "Custom";
	c.replay.filePath = "/clips/r.mp4";
	c.replay.mediaUuid = "uuid-m";
	c.highlights.graphicsUrl = "http://x/y";
	c.highlights.graphicsEnabled = true;
	c.graphicsUrl = "https://graphics.example/out?token=secret";
	c.graphicsUuid = "uuid-g";
	c.graphicsVisible = false;
	c.graphicsProgramOnly = false;
	c.lastLiveCameraUuid = "uuid-s1";

	obs_data_t *d = obs_data_create();
	saveConfig(c, d);
	// Through JSON as OBS would store it in the scene collection.
	const char *json = obs_data_get_json(d);
	obs_data_t *d2 = obs_data_create_from_json(json);
	BankConfig r = loadConfig(d2, "other");
	obs_data_release(d2);
	obs_data_release(d);

	CHECK_EQ(r.bankId, std::string("bank123"));
	CHECK_EQ(r.cameras[0].sceneUuid, std::string("uuid-s1"));
	CHECK_EQ(r.cameras[0].inputUuid, std::string("uuid-i1"));
	CHECK_EQ(r.cameras[0].inputTypeId, std::string("v4l2_input"));
	CHECK_EQ(r.cameras[0].inputItemId, 7);
	CHECK_EQ(r.cameras[0].graphicsItemId, 9);
	CHECK_EQ(r.cameras[3].label, std::string("Custom"));
	CHECK_EQ(r.cameras[5].label, std::string("Reverse Angle"));
	CHECK_EQ(r.replay.filePath, std::string("/clips/r.mp4"));
	CHECK_EQ(r.replay.mediaUuid, std::string("uuid-m"));
	CHECK(r.highlights.graphicsEnabled);
	CHECK_EQ(r.graphicsUuid, std::string("uuid-g"));
	CHECK(!r.graphicsVisible);
	CHECK(!r.graphicsProgramOnly);
	CHECK_EQ(r.lastLiveCameraUuid, std::string("uuid-s1"));
	CHECK(!r.migratedFromV1);
}

TEST(config_defaults_when_missing)
{
	BankConfig r = loadConfig(nullptr, "fresh");
	CHECK_EQ(r.bankId, std::string("fresh"));
	CHECK_EQ(r.cameras[0].label, std::string("Main Wide"));
	CHECK_EQ(r.cameras[7].sceneName, std::string("MF — CAM 08"));
	CHECK_EQ(r.replay.sceneName, std::string("MF — REPLAY"));
	CHECK_EQ(r.highlights.sceneName, std::string("MF — HIGHLIGHTS"));
	CHECK(r.graphicsVisible);
	CHECK(r.graphicsProgramOnly); // existing banks default to clean multiview tiles
	obs_data_t *empty = obs_data_create();
	BankConfig e = loadConfig(empty, "fresh2");
	obs_data_release(empty);
	CHECK_EQ(e.bankId, std::string("fresh2"));
}

TEST(config_migrates_v1_names)
{
	const char *v1 = R"({"version":1,"graphics_url":"http://g/out",
		"replay_file":"/r.mp4","highlights_file":"/h.mp4",
		"cameras":[{"label":"Wide","scene":"MF — CAM 01","input":"Camera A"},
		           {"label":"Tight","scene":"MF — CAM 02","input":"Camera B"}]})";
	obs_data_t *d = obs_data_create_from_json(v1);
	BankConfig c = loadConfig(d, "newbank");
	obs_data_release(d);
	CHECK(c.migratedFromV1);
	CHECK_EQ(c.bankId, std::string("newbank"));
	CHECK_EQ(c.cameras[0].label, std::string("Wide"));
	CHECK_EQ(c.cameras[1].inputName, std::string("Camera B"));
	CHECK(c.cameras[1].inputUuid.empty()); // resolved (and recorded) by the scene bank later
	CHECK_EQ(c.cameras[2].label, std::string("Left Goal"));
	CHECK_EQ(c.graphicsUrl, std::string("http://g/out"));
	CHECK_EQ(c.replay.filePath, std::string("/r.mp4"));
	CHECK_EQ(c.highlights.filePath, std::string("/h.mp4"));
	// Saving writes the current schema.
	obs_data_t *out = obs_data_create();
	saveConfig(c, out);
	CHECK_EQ(obs_data_get_int(out, "version"), (long long)kConfigVersion);
	obs_data_release(out);
}

TEST(config_extra_cameras_ignored)
{
	obs_data_t *d = obs_data_create();
	obs_data_set_int(d, "version", 2);
	obs_data_array_t *arr = obs_data_array_create();
	for (int i = 0; i < 12; i++) {
		obs_data_t *c = obs_data_create();
		obs_data_set_string(c, "label", ("L" + std::to_string(i)).c_str());
		obs_data_array_push_back(arr, c);
		obs_data_release(c);
	}
	obs_data_set_array(d, "cameras", arr);
	obs_data_array_release(arr);
	BankConfig c = loadConfig(d, "b");
	obs_data_release(d);
	CHECK_EQ(c.cameras[7].label, std::string("L7"));
}

TEST(redact_url)
{
	CHECK_EQ(redactUrl("https://gfx.example.com/output/abc123token?key=SECRET#x"),
		 std::string("https://gfx.example.com/output/…?…"));
	CHECK_EQ(redactUrl("https://user:pass@host.example/p"), std::string("https://***@host.example/p"));
	CHECK_EQ(redactUrl("http://localhost:8080/"), std::string("http://localhost:8080/"));
	CHECK_EQ(redactUrl(""), std::string(""));
	std::string r = redactUrl("https://h/a/b/c?token=abc");
	CHECK(r.find("token") == std::string::npos);
	CHECK(r.find("abc") == std::string::npos);
	CHECK(r.find("/b") == std::string::npos);
}

TEST(default_names)
{
	CHECK_EQ(std::string(defaultCameraSceneName(0)), std::string("MF — CAM 01"));
	CHECK_EQ(std::string(defaultCameraLabel(7)), std::string("Handheld / Spare"));
	CHECK_EQ(std::string(defaultPlaybackSceneName(9)), std::string("MF — HIGHLIGHTS"));
	CHECK(randomId().size() == 16);
	CHECK(randomId() != randomId());
}

// =============================================================================
// MatchFrame Replay Cue link

// Shapes copied from Replay Cue's GetStatus / GetEvents responses (websocket-api.cpp,
// ReplayController::poll snapshot).
static const char *kRcStatusJson = R"({"ok":true,"armed":true,"pending_replays":1,
	"selected_event":"e2",
	"playback":{"state":"paused","event_id":"e2","event_name":"Goal 1","angle":2,
		"angle_label":"Cam 2","angle_count":4,"speed":0.5,"position":3.2,"duration":8.0,
		"cued":true,"highlights":false},
	"match":{"sport":"football","title":"Match 1: Leeds vs York","phase":1,
		"phase_name":"Half Time","in_break":true}})";

TEST(replay_cue_status_parse)
{
	obs_data_t *d = obs_data_create_from_json(kRcStatusJson);
	ReplayCueStatus s = parseReplayCueStatus(d);
	obs_data_release(d);
	CHECK(s.ok);
	CHECK(s.armed);
	CHECK_EQ(s.pendingReplays, 1);
	CHECK_EQ(s.selectedEvent, std::string("e2"));
	CHECK_EQ(s.state, std::string("paused"));
	CHECK_EQ(s.eventName, std::string("Goal 1"));
	CHECK_EQ(s.angle, 2);
	CHECK_EQ(s.angleCount, 4);
	CHECK(std::fabs(s.speed - 0.5) < 1e-9);
	CHECK(s.cued);
	CHECK(!s.highlights);
	CHECK_EQ(s.phaseName, std::string("Half Time"));
	CHECK(s.inBreak);
	std::string text = describeReplayCuePlayback(s);
	CHECK_EQ(text, std::string("CUED Goal 1 · Cam 2 · 50% · 0:03/0:08"));

	obs_data_t *f = obs_data_create_from_json(R"({"ok":false,"error":"Replay Cue is shutting down"})");
	ReplayCueStatus e = parseReplayCueStatus(f);
	obs_data_release(f);
	CHECK(!e.ok);
	CHECK_EQ(e.error, std::string("Replay Cue is shutting down"));
	CHECK(!parseReplayCueStatus(nullptr).ok);
	CHECK_EQ(describeReplayCuePlayback(ReplayCueStatus()), std::string("Replay Cue not available"));

	ReplayCueStatus idle;
	idle.ok = true;
	idle.armed = true;
	CHECK_EQ(describeReplayCuePlayback(idle), std::string("ARMED · no replay loaded"));
	idle.eventName = "Save";
	idle.state = "playing";
	CHECK_EQ(describeReplayCuePlayback(idle), std::string("PLAYING Save"));
}

TEST(replay_cue_event_count)
{
	obs_data_t *d = obs_data_create_from_json(R"({"ok":true,"events":[{"id":"a"},{"id":"b"},{"id":"c"}]})");
	CHECK_EQ(parseReplayCueEventCount(d), 3);
	obs_data_release(d);
	obs_data_t *e = obs_data_create_from_json(R"({"ok":true})");
	CHECK_EQ(parseReplayCueEventCount(e), -1);
	obs_data_release(e);
	CHECK_EQ(parseReplayCueEventCount(nullptr), -1);
}

TEST(replay_cue_scene_pick)
{
	std::vector<SceneCandidate> none = {{"Scene", false}, {"MF — CAM 01", false}};
	auto p = pickReplayCueScenes(none, "", "");
	CHECK(p.first.empty() && p.second.empty()); // not created yet

	std::vector<SceneCandidate> def = {{"Scene", false}, {"Replay", true}};
	p = pickReplayCueScenes(def, "", "");
	CHECK_EQ(p.first, std::string("Replay"));
	CHECK_EQ(p.second, std::string("Replay")); // no separate highlights scene: Replay Cue uses the replay scene

	std::vector<SceneCandidate> both = {{"Replay", true}, {"HT Highlights", true}, {"Scene", false}};
	p = pickReplayCueScenes(both, "", "");
	CHECK_EQ(p.first, std::string("Replay"));
	CHECK_EQ(p.second, std::string("HT Highlights"));

	std::vector<SceneCandidate> renamed = {{"Instant", true}, {"Scene", false}};
	p = pickReplayCueScenes(renamed, "", "");
	CHECK_EQ(p.first, std::string("Instant"));

	// Configured names win; unknown configured names fall back to detection.
	p = pickReplayCueScenes(both, "HT Highlights", "Replay");
	CHECK_EQ(p.first, std::string("HT Highlights"));
	CHECK_EQ(p.second, std::string("Replay"));
	p = pickReplayCueScenes(both, "Gone", "Gone too");
	CHECK_EQ(p.first, std::string("Replay"));
	CHECK_EQ(p.second, std::string("HT Highlights"));

	// Several extra player scenes: ambiguous, so highlights stays on the replay scene.
	std::vector<SceneCandidate> many = {{"Replay", true}, {"A", true}, {"B", true}};
	p = pickReplayCueScenes(many, "", "");
	CHECK_EQ(p.second, std::string("Replay"));
}

TEST(replay_cue_config_roundtrip)
{
	BankConfig c = makeDefaultConfig("b");
	CHECK_EQ(c.playbackProvider, std::string("builtin"));
	c.playbackProvider = "replaycue";
	c.replayCueReplayScene = "Replay";
	c.replayCueHighlightsScene = "HT Highlights";
	obs_data_t *d = obs_data_create();
	saveConfig(c, d);
	BankConfig r = loadConfig(d, "x");
	obs_data_release(d);
	CHECK_EQ(r.playbackProvider, std::string("replaycue"));
	CHECK_EQ(r.replayCueReplayScene, std::string("Replay"));
	CHECK_EQ(r.replayCueHighlightsScene, std::string("HT Highlights"));
	// Older v2 configs without the field keep the built-in provider; junk values are ignored.
	obs_data_t *old = obs_data_create_from_json(R"({"version":2,"bank_id":"o","playback_provider":"bogus"})");
	CHECK_EQ(loadConfig(old, "x").playbackProvider, std::string("builtin"));
	obs_data_release(old);
}

int main()
{
	for (auto &t : tests()) {
		int before = g_failures;
		t.fn();
		std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name);
	}
	std::printf("\n%zu tests, %d checks, %d failures\n", tests().size(), g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
