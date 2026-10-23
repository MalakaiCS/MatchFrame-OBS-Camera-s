// MatchFrame Multiview - in-OBS automated self-test. See selftest.hpp.
//
// Environment:
//   MATCHFRAME_SELFTEST_OUT      output JSON path (enables the test)
//   MATCHFRAME_SELFTEST_PHASE    "1" (fresh profile: full run, default) or "2" (after restart)
//   MATCHFRAME_SELFTEST_MEDIA    directory with cam1..8.mp4, replay.mp4, highlights.mp4, commentary.wav
//   MATCHFRAME_SELFTEST_RECDIR   directory for the Program recording (phase 1)
//   MATCHFRAME_SELFTEST_PERF_SECONDS  duration of each performance window (default 30)
//   MATCHFRAME_SELFTEST_SOAK_SECONDS  optional soak run with repeated takes (default 0)
//   MATCHFRAME_SELFTEST_OFFAIR_SECONDS  Preview-wait duration for the off-air test (default 60)
//   MATCHFRAME_TEST_GRAPHICS_SOURCE    stand-in source type when Browser Source is unavailable

#include "selftest.hpp"

#include "controller.hpp"
#include "multiview-window.hpp"
#include "obs-util.hpp"
#include "core/replay-cue-status.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <media-io/video-io.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QTimer>

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace mf {

namespace {

std::atomic<int> g_sceneChanged{0};
std::atomic<int> g_replayStarted{0};
std::atomic<int> g_highlightsStarted{0};

QString env(const char *k, const char *def = "")
{
	const char *v = getenv(k);
	return QString::fromUtf8(v && *v ? v : def);
}

class SelfTest : public QObject {
public:
	SelfTest()
	{
		out_ = env("MATCHFRAME_SELFTEST_OUT");
		phase_ = env("MATCHFRAME_SELFTEST_PHASE", "1").toInt();
		media_ = env("MATCHFRAME_SELFTEST_MEDIA");
		recDir_ = env("MATCHFRAME_SELFTEST_RECDIR", "/tmp");
		perfSeconds_ = env("MATCHFRAME_SELFTEST_PERF_SECONDS", "30").toInt();
		soakSeconds_ = env("MATCHFRAME_SELFTEST_SOAK_SECONDS", "0").toInt();
		offAirSeconds_ = env("MATCHFRAME_SELFTEST_OFFAIR_SECONDS", "60").toInt();
		timer_.setInterval(20);
		connect(&timer_, &QTimer::timeout, this, [this]() { tick(); });
		if (phase_ == 2)
			buildPhase2();
		else if (phase_ == 3)
			buildPhase3();
		else
			buildPhase1();
	}

	void start()
	{
		log("self-test phase %d starting (%zu steps)", phase_, steps_.size());
		t0_ = nowMs();
		timer_.start();
	}

	void onEvent(enum obs_frontend_event e)
	{
		if (e == OBS_FRONTEND_EVENT_SCENE_CHANGED)
			g_sceneChanged++;
		if (e == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
			recStartMs_ = nowMs();
			mark("recording_started");
		}
		if (e == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
			collectionChanges_++;
	}

private:
	struct Step {
		QString name;
		std::function<void()> run;
		std::function<bool()> until;
		int timeoutMs;
	};

	// ------------------------------------------------------------------ runner
	template<typename... A> void log(const char *fmt, A... a)
	{
		obs_log(LOG_INFO, (std::string("[selftest] ") + fmt).c_str(), a...);
	}

	void add(const QString &name, std::function<void()> run, std::function<bool()> until = nullptr,
		 int timeoutMs = 10000)
	{
		steps_.push_back({name, std::move(run), std::move(until), timeoutMs});
	}
	void waitFor(const QString &name, std::function<bool()> until, int timeoutMs = 10000)
	{
		add(name, nullptr, std::move(until), timeoutMs);
	}
	void sleep(int ms)
	{
		auto deadline = std::make_shared<int64_t>(0);
		add(
			QString("sleep %1 ms").arg(ms), [deadline, ms]() { *deadline = nowMs() + ms; },
			[deadline]() { return nowMs() >= *deadline; }, ms + 5000);
	}
	// Repeats `sample` every tick for `ms`.
	void during(const QString &name, int ms, std::function<void()> sample)
	{
		auto deadline = std::make_shared<int64_t>(0);
		add(
			name, [deadline, ms]() { *deadline = nowMs() + ms; },
			[deadline, sample]() {
				sample();
				return nowMs() >= *deadline;
			},
			ms + 10000);
	}

	void tick()
	{
		if (busy_)
			return;
		busy_ = true;
		while (idx_ < steps_.size()) {
			Step &s = steps_[idx_];
			if (!stepStarted_) {
				stepStarted_ = true;
				stepStartMs_ = nowMs();
				if (s.run)
					s.run();
			}
			if (s.until && !s.until()) {
				if (nowMs() - stepStartMs_ > s.timeoutMs) {
					check("timeout", "Step completed in time: " + s.name, false,
					      QString("timed out after %1 ms").arg(s.timeoutMs));
				} else {
					busy_ = false;
					return; // poll again next tick
				}
			}
			idx_++;
			stepStarted_ = false;
		}
		timer_.stop();
		busy_ = false;
		finish();
	}

	void check(const QString &id, const QString &title, bool pass, const QString &detail = QString())
	{
		QJsonObject o;
		o["id"] = id;
		o["title"] = title;
		o["pass"] = pass;
		o["detail"] = detail;
		o["t_ms"] = (double)(nowMs() - t0_);
		results_.append(o);
		obs_log(pass ? LOG_INFO : LOG_WARNING, "[selftest] %s %s: %s %s", pass ? "PASS" : "FAIL",
			id.toUtf8().constData(), title.toUtf8().constData(), detail.toUtf8().constData());
	}

	void mark(const QString &event, const QString &detail = QString())
	{
		QJsonObject o;
		o["event"] = event;
		o["detail"] = detail;
		o["t_ms"] = (double)(nowMs() - t0_);
		o["rec_ms"] = recStartMs_ ? (double)(nowMs() - recStartMs_) : -1.0;
		timeline_.append(o);
	}

	void finish()
	{
		int pass = 0, fail = 0;
		for (const auto &r : results_)
			(r.toObject()["pass"].toBool() ? pass : fail)++;
		QJsonObject root;
		root["phase"] = phase_;
		root["obs_version"] = obs_get_version_string();
		root["libobs_api_built"] = QString("%1.%2.%3")
						   .arg(LIBOBS_API_MAJOR_VER)
						   .arg(LIBOBS_API_MINOR_VER)
						   .arg(LIBOBS_API_PATCH_VER);
		root["qt_runtime"] = qVersion();
		root["graphics_source_type"] = graphicsSourceTypeId();
		root["browser_source_available"] = sourceTypeAvailable("browser_source");
		root["results"] = results_;
		root["timeline"] = timeline_;
		root["perf"] = perf_;
		root["recording"] = recordingPath_;
		root["pass"] = pass;
		root["fail"] = fail;
		QFile f(out_);
		if (f.open(QIODevice::WriteOnly))
			f.write(QJsonDocument(root).toJson());
		parked_.clear(); // the test's own references must not outlive the collection
		log("self-test phase %d finished: %d passed, %d failed -> %s", phase_, pass, fail,
		    out_.toUtf8().constData());
		QTimer::singleShot(500, []() {
			auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (main)
				main->close();
		});
	}

	// ------------------------------------------------------------------ helpers
	Controller *ctl() { return Controller::instance(); }
	QString mediaPath(const QString &f) { return QDir(media_).filePath(f); }

	std::string sceneUuid(int slot)
	{
		OBSSourceAutoRelease s = ctl()->bank().slotScene(slot);
		return uuidOf(s);
	}
	std::string programUuid()
	{
		OBSSourceAutoRelease p = obs_frontend_get_current_scene();
		return uuidOf(p);
	}
	std::string previewUuid()
	{
		OBSSourceAutoRelease p = obs_frontend_get_current_preview_scene();
		return uuidOf(p);
	}
	bool programIs(int slot) { return !sceneUuid(slot).empty() && programUuid() == sceneUuid(slot); }

	int countOwnedScenes()
	{
		int n = 0;
		obs_enum_scenes(
			[](void *p, obs_source_t *s) {
				if (isOwnedByAnyBank(s))
					(*static_cast<int *>(p))++;
				return true;
			},
			&n);
		return n;
	}
	int countSources()
	{
		int n = 0;
		obs_enum_sources(
			[](void *p, obs_source_t *) {
				(*static_cast<int *>(p))++;
				return true;
			},
			&n);
		obs_enum_scenes(
			[](void *p, obs_source_t *) {
				(*static_cast<int *>(p))++;
				return true;
			},
			&n);
		return n;
	}
	// Scenes (other than the eight camera scenes) that contain the camera graphics source.
	int graphicsOutsideCameraScenes(obs_source_t *g)
	{
		struct Ctx {
			obs_source_t *g;
			std::set<std::string> cams;
			int n = 0;
		} ctx{g, {}};
		for (int i = 0; i < 8; i++)
			ctx.cams.insert(sceneUuid(i));
		obs_enum_scenes(
			[](void *p, obs_source_t *s) {
				auto *c = static_cast<Ctx *>(p);
				if (c->cams.count(uuidOf(s)))
					return true;
				if (!itemsWithSource(obs_scene_from_source(s), c->g).empty())
					c->n++;
				return true;
			},
			&ctx);
		return ctx.n;
	}

	std::string structureFingerprint()
	{
		std::string fp;
		for (int i = 0; i < 10; i++) {
			OBSSourceAutoRelease s = ctl()->bank().slotScene(i);
			fp += uuidOf(s) + "[";
			for (auto *item : sceneItems(obs_scene_from_source(s)))
				fp += std::to_string(obs_sceneitem_get_id(item)) + ":" +
				      uuidOf(obs_sceneitem_get_source(item)) + ",";
			fp += "]";
		}
		return fp;
	}

	OBSSourceAutoRelease media(int slot) { return ctl()->bank().mediaSource(slot); }
	int64_t mediaTime(int slot)
	{
		OBSSourceAutoRelease m = media(slot);
		return m ? obs_source_media_get_time(m) : -1;
	}
	PlaybackState pbState(int slot) { return ctl()->playback().logic(slot).state(); }

	OBSSourceAutoRelease createInput(const char *id, const QString &name, obs_data_t *settings)
	{
		OBSSourceAutoRelease s = obs_source_create(id, name.toUtf8().constData(), settings, nullptr);
		return s;
	}

	void connectMediaCounters()
	{
		for (int slot : {kSlotReplay, kSlotHighlights}) {
			OBSSourceAutoRelease m = media(slot);
			if (!m)
				continue;
			signal_handler_t *sh = obs_source_get_signal_handler(m);
			signal_handler_disconnect(sh, "media_started",
						  slot == kSlotReplay ? onReplayStarted : onHighlightsStarted, nullptr);
			signal_handler_connect(sh, "media_started",
					       slot == kSlotReplay ? onReplayStarted : onHighlightsStarted, nullptr);
		}
	}
	static void onReplayStarted(void *, calldata_t *) { g_replayStarted++; }
	static void onHighlightsStarted(void *, calldata_t *) { g_highlightsStarted++; }

	void sendKey(int key, bool autoRepeat = false, const QString &text = QString())
	{
		MultiviewWindow *w = ctl()->window();
		if (!w)
			return;
		QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, text, autoRepeat);
		QCoreApplication::sendEvent(w, &ev);
	}

	void cutTo(int slot, const QString &why)
	{
		add(
			"cut to " + why,
			[this, slot]() {
				ctl()->selectSlot(slot);
				ctl()->cut();
			},
			[this, slot]() { return programIs(slot) && !ctl()->switcher().takeState().busy(); }, 5000);
	}

	// ------------------------------------------------------------------ phase 1
	void buildPhase1()
	{
		auto cams = std::make_shared<std::vector<std::string>>(); // input uuids
		auto expectedLastCam = std::make_shared<int>(-1);

		add(
			"studio mode", []() { obs_frontend_set_preview_program_mode(true); },
			[]() { return obs_frontend_preview_program_mode_active(); });

		add("create test inputs", [this, cams]() {
			// Recording destination for the audio/visual analysis.
			config_t *prof = obs_frontend_get_profile_config();
			config_set_string(prof, "Output", "Mode", "Simple");
			config_set_string(prof, "SimpleOutput", "FilePath", recDir_.toUtf8().constData());
			config_set_string(prof, "SimpleOutput", "RecFormat2", "mkv");
			config_set_string(prof, "SimpleOutput", "RecFormat", "mkv");

			for (int i = 1; i <= 8; i++) {
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_bool(s, "is_local_file", true);
				obs_data_set_string(s, "local_file",
						    mediaPath(QString("cam%1.mp4").arg(i)).toUtf8().constData());
				obs_data_set_bool(s, "looping", true);
				obs_data_set_bool(s, "restart_on_activate", false);
				obs_data_set_bool(s, "close_when_inactive", false);
				OBSSourceAutoRelease src =
					createInput("ffmpeg_source", QString("TEST CAM %1").arg(i), s);
				cams->push_back(uuidOf(src));
				// keep alive: parked in an unrelated scene
				parked_.push_back(OBSSourceAutoRelease(obs_source_get_ref(src)));
			}
			// Continuous "commentary": a global output channel, like OBS's Mic/Aux devices.
			OBSDataAutoRelease cs = obs_data_create();
			obs_data_set_bool(cs, "is_local_file", true);
			obs_data_set_string(cs, "local_file", mediaPath("commentary.wav").toUtf8().constData());
			obs_data_set_bool(cs, "looping", true);
			obs_data_set_bool(cs, "restart_on_activate", false);
			OBSSourceAutoRelease comm = createInput("ffmpeg_source", "TEST Commentary", cs);
			obs_set_output_source(6, comm);

			// An unrelated object occupying a default name (name-collision handling).
			OBSDataAutoRelease col = obs_data_create();
			obs_data_set_int(col, "color", 0xFF00FF00);
			OBSSourceAutoRelease squatter = createInput("color_source_v3", "MF — CAM 05", col);
			parked_.push_back(OBSSourceAutoRelease(obs_source_get_ref(squatter)));

			// An unrelated scene with the inputs, so they live in the collection.
			obs_scene_t *park = obs_scene_create("Unrelated Test Inputs");
			for (auto &p : parked_)
				obs_scene_add(park, p);
			obs_scene_release(park);
			startCount_ = countSources();
		});

		add("assign cameras 1-7 (camera 8 left unassigned)", [this, cams]() {
			ctl()->config().graphicsUrl = "http://127.0.0.1:8765/overlay.html?token=SELFTEST-SECRET";
			for (int i = 0; i < 7; i++) {
				QString msg;
				ctl()->assignInput(i, (*cams)[i], &msg);
			}
		});

		add("create / repair", [this]() {
			RepairReport r = ctl()->createOrRepair();
			repairLines_ = r.lines;
			mark("repair1");
			// Stand-in graphics: a translucent colour source (the browser keys are ignored by it).
			OBSSourceAutoRelease g = ctl()->bank().cameraGraphics();
			if (g && strcmp(graphicsSourceTypeId(), "browser_source") != 0) {
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_int(s, "color", 0x30FFFFFF);
				obs_source_update(g, s);
			}
			connectMediaCounters();
		});

		add("C01 ten production scenes", [this]() {
			int owned = countOwnedScenes();
			bool names = true;
			QString list;
			for (int i = 0; i < 10; i++) {
				OBSSourceAutoRelease s = ctl()->bank().slotScene(i);
				names = names && s;
				list += QString::fromStdString(nameOf(s)) + "; ";
			}
			check("C01", "Exactly ten plugin-owned scenes (8 cameras + Replay + Highlights)",
			      owned == 10 && names, QString("owned=%1: %2").arg(owned).arg(list));
			OBSSourceAutoRelease s5 = ctl()->bank().slotScene(4);
			bool collisionHandled = nameOf(s5) == "MF — CAM 05 (2)";
			bool warned = false;
			for (auto &l : repairLines_)
				warned = warned || l.find("unrelated object") != std::string::npos;
			check("C01b",
			      "Name collision with an unrelated object uses a safe alternative name and is reported",
			      collisionHandled && warned, QString::fromStdString(nameOf(s5)));
		});

		add("C02/C03 scene contents", [this]() {
			OBSSourceAutoRelease g = ctl()->bank().cameraGraphics();
			bool ok = g != nullptr;
			QString detail;
			for (int i = 0; i < 8; i++) {
				OBSSourceAutoRelease s = ctl()->bank().slotScene(i);
				auto items = sceneItems(obs_scene_from_source(s));
				QString row;
				for (auto *it : items)
					row += QString::fromStdString(nameOf(obs_sceneitem_get_source(it))) + " < ";
				detail += QString("CAM%1: %2| ").arg(i + 1).arg(row);
				if (i < 7) {
					ok = ok && items.size() == 2 && obs_sceneitem_get_source(items[0]) != g.Get() &&
					     uuidOf(obs_sceneitem_get_source(items[0])) ==
						     ctl()->config().cameras[i].inputUuid &&
					     obs_sceneitem_get_source(items[1]) == g.Get();
				} else {
					ok = ok && items.size() == 1 && obs_sceneitem_get_source(items[0]) == g.Get();
				}
			}
			check("C03",
			      "Each camera scene: its own input at the bottom, the shared graphics item directly above",
			      ok, detail);
			check("C02",
			      "One graphics source instance shared by all eight camera scenes; no graphics helper scene",
			      g && graphicsOutsideCameraScenes(g) == 0 && countOwnedScenes() == 10,
			      QString("graphics source '%1' (%2)")
				      .arg(QString::fromStdString(nameOf(g)), graphicsSourceTypeId()));

			bool pbOk = true;
			for (int slot : {kSlotReplay, kSlotHighlights}) {
				OBSSourceAutoRelease s = ctl()->bank().slotScene(slot);
				OBSSourceAutoRelease m = media(slot);
				auto items = sceneItems(obs_scene_from_source(s));
				pbOk = pbOk && m && items.size() == 1 && obs_sceneitem_get_source(items[0]) == m.Get();
				OBSDataAutoRelease st = obs_source_get_settings(m);
				pbOk = pbOk && !obs_data_get_bool(st, "restart_on_activate") &&
				       !obs_data_get_bool(st, "looping") &&
				       !obs_data_get_bool(st, "close_when_inactive") &&
				       !obs_data_get_bool(st, "clear_on_media_end") &&
				       obs_source_get_monitoring_type(m) == OBS_MONITORING_TYPE_NONE;
			}
			OBSSourceAutoRelease r = media(kSlotReplay), h = media(kSlotHighlights);
			check("C04",
			      "Replay/Highlights scenes each hold their own media source (no camera graphics); "
			      "restart-on-activate/loop/close-when-inactive OFF, monitoring OFF",
			      pbOk && r.Get() != h.Get());

			OBSDataAutoRelease gs = obs_source_get_settings(g);
			uint32_t w, hgt;
			canvasSize(w, hgt);
			check("C05",
			      "Graphics source: shutdown-when-hidden OFF, refresh-on-active OFF, sized to canvas",
			      g && !obs_data_get_bool(gs, "shutdown") &&
				      !obs_data_get_bool(gs, "restart_when_active") &&
				      obs_data_get_int(gs, "width") == w && obs_data_get_int(gs, "height") == hgt,
			      QString("%1x%2").arg(obs_data_get_int(gs, "width")).arg(obs_data_get_int(gs, "height")));
		});

		add("C06 repair idempotent + preserves manual edits", [this]() {
			OBSSourceAutoRelease s1 = ctl()->bank().slotScene(0);
			OBSSourceAutoRelease in1 = ctl()->bank().cameraInput(0);
			obs_sceneitem_t *it = ctl()->bank().cameraItem(0, obs_scene_from_source(s1), in1);
			vec2 pos{};
			pos.x = 123;
			pos.y = 45;
			obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_NONE);
			obs_sceneitem_set_pos(it, &pos);
			vec2 sc{};
			sc.x = 0.5f;
			sc.y = 0.5f;
			obs_sceneitem_set_scale(it, &sc);
			obs_sceneitem_crop crop = {10, 0, 0, 20};
			obs_sceneitem_set_crop(it, &crop);
			// Push cam2's graphics below its camera: repair must restore the order.
			OBSSourceAutoRelease s2 = ctl()->bank().slotScene(1);
			OBSSourceAutoRelease g = ctl()->bank().cameraGraphics();
			obs_sceneitem_t *g2 = ctl()->bank().graphicsItem(1, obs_scene_from_source(s2), g);
			obs_sceneitem_set_order(g2, OBS_ORDER_MOVE_BOTTOM);
			OBSSourceAutoRelease in2 = ctl()->bank().cameraInput(1);
			obs_sceneitem_t *c2 = ctl()->bank().cameraItem(1, obs_scene_from_source(s2), in2);
			vec2 pos2{};
			pos2.x = 77;
			obs_sceneitem_set_pos(c2, &pos2);

			int before = countSources();
			RepairReport a = ctl()->createOrRepair();
			std::string fp1 = structureFingerprint();
			RepairReport b = ctl()->createOrRepair();
			RepairReport c = ctl()->createOrRepair();
			std::string fp3 = structureFingerprint();
			int after = countSources();

			vec2 p{}, s{};
			obs_sceneitem_get_pos(it, &p);
			obs_sceneitem_get_scale(it, &s);
			obs_sceneitem_crop cr{};
			obs_sceneitem_get_crop(it, &cr);
			bool preserved = p.x == 123 && p.y == 45 && s.x == 0.5f && cr.left == 10 && cr.bottom == 20;
			bool orderFixed = itemIndex(obs_scene_from_source(s2), g2) >
					  itemIndex(obs_scene_from_source(s2), c2);
			vec2 p2{};
			obs_sceneitem_get_pos(c2, &p2);
			check("C06", "Re-running Create / Repair creates no duplicates and preserves manual transforms",
			      before == after && fp1 == fp3 && b.created == 0 && c.created == 0 &&
				      countOwnedScenes() == 10 && preserved,
			      QString("sources %1->%2, created on reruns %3/%4/%5, transform kept=%6")
				      .arg(before)
				      .arg(after)
				      .arg(a.created)
				      .arg(b.created)
				      .arg(c.created)
				      .arg(preserved));
			check("C07",
			      "Graphics stay above the camera after per-camera edits (repair restores order, keeps framing)",
			      orderFixed && p2.x == 77, QString("graphics index above camera=%1").arg(orderFixed));
			// restore Fit framing on cam1 for the visual checks
			std::string m;
			ctl()->bank().applyFraming(0, Framing::Fit, &m);
		});

		add(
			"open multiview", [this]() { ctl()->showMultiview(); },
			[this]() { return ctl()->window() && ctl()->window()->surface()->displayValid(); }, 8000);

		// ---- Preview isolation
		add("C08 preview isolation", [this]() {
			std::string prog = programUuid();
			bool ok = true;
			QString detail;
			for (int i = 0; i < 7; i++) {
				bool sel = ctl()->selectSlot(i);
				ok = ok && sel && previewUuid() == sceneUuid(i) && programUuid() == prog;
			}
			bool sel8 = ctl()->selectSlot(7); // unassigned
			bool previewKept = previewUuid() == sceneUuid(6);
			check("C08", "Selecting any valid tile changes Preview only; Program unchanged", ok,
			      QString("program stayed '%1'").arg(QString::fromStdString(prog)));
			check("C09", "Unassigned camera slot cannot be selected (partial bank)", !sel8 && previewKept,
			      ctl()->lastMessage());
		});
		add("C11b repair clears overrides", [this]() {
			OBSSourceAutoRelease s4 = ctl()->bank().slotScene(3);
			OBSDataAutoRelease priv = obs_source_get_private_settings(s4);
			obs_data_set_string(priv, "transition", "Fade");
			ctl()->createOrRepair();
			OBSDataAutoRelease priv2 = obs_source_get_private_settings(s4);
			check("C11b", "Create / Repair clears transition overrides on the ten owned scenes",
			      strlen(obs_data_get_string(priv2, "transition")) == 0);
		});
		add("assign camera 8", [this, cams]() {
			QString m;
			bool ok = ctl()->assignInput(7, (*cams)[7], &m);
			check("C09b", "Assigning the eighth camera completes the bank (1-8 cameras supported)",
			      ok && ctl()->switcher().slotFacts(7).inputExists);
		});
		add(
			"start recording", []() { obs_frontend_recording_start(); },
			[]() { return obs_frontend_recording_active(); }, 10000);
		add("C39 structural setup refused while recording", [this, cams]() {
			std::string fp = structureFingerprint();
			RepairReport r = ctl()->createOrRepair();
			QString msg;
			bool assigned = ctl()->assignInput(6, (*cams)[0], &msg);
			check("C39",
			      "Create / Repair and input assignment are refused while recording; nothing changes",
			      r.warnings > 0 && r.created == 0 && !assigned && fp == structureFingerprint(), msg);
		});
		sleep(1500);

		// ---- Hard cut with Fade selected beforehand
		add("select fade transition", []() {
			obs_frontend_source_list l = {};
			obs_frontend_get_transitions(&l);
			for (size_t i = 0; i < l.sources.num; i++)
				if (strcmp(obs_source_get_unversioned_id(l.sources.array[i]), "fade_transition") == 0)
					obs_frontend_set_current_transition(l.sources.array[i]);
			obs_frontend_source_list_free(&l);
		});
		add(
			"C10 hard cut",
			[this, expectedLastCam]() {
				ctl()->selectSlot(1);
				cutStart_ = nowMs();
				sceneChangedBefore_ = g_sceneChanged;
				ctl()->cut();
				*expectedLastCam = 1;
				mark("cut", "CAM 2");
			},
			[this]() { return programIs(1) && !ctl()->switcher().takeState().busy(); }, 3000);
		add("C10 verify", [this]() {
			OBSSourceAutoRelease tr = obs_frontend_get_current_transition();
			OBSSourceAutoRelease out = obs_get_output_source(0);
			check("C10", "CUT moves Preview to Program with the Cut transition (no fade)",
			      programIs(1) && strcmp(obs_source_get_unversioned_id(tr), "cut_transition") == 0 &&
				      out.Get() == tr.Get(),
			      QString("program after %1 ms; transition '%2'")
				      .arg(nowMs() - cutStart_)
				      .arg(QString::fromStdString(nameOf(tr))));
		});

		// ---- Per-scene override conflicts
		add("C11 transition override", [this]() {
			OBSSourceAutoRelease s3 = ctl()->bank().slotScene(2);
			OBSDataAutoRelease priv = obs_source_get_private_settings(s3);
			OBSSourceAutoRelease fade = findTransitionByName("Fade");
			obs_data_set_string(priv, "transition", fade ? "Fade" : "Fade");
			obs_data_set_int(priv, "transition_duration", 800);
			ctl()->selectSlot(2);
			bool rejected = !ctl()->cut();
			QString msg = ctl()->lastMessage();
			bool programKept = programIs(1);
			check("C11", "A per-scene non-Cut override blocks CUT with a clear message (no silent fade)",
			      rejected && programKept && msg.contains("override"), msg);
			clearTransitionOverride(s3); // operator corrects it
		});
		cutTo(2, "CAM 3");

		// ---- Repeated CUT suppression
		add("C12 repeated CUT", [this, expectedLastCam]() {
			ctl()->selectSlot(3);
			programChanges_ = 0;
			lastProgramSeen_ = programUuid();
			uint64_t droppedBefore = ctl()->switcher().takeState().dropped();
			int accepted = 0;
			for (int i = 0; i < 20; i++)
				accepted += ctl()->cut() ? 1 : 0;
			repeatAccepted_ = accepted;
			repeatDropped_ = (int)(ctl()->switcher().takeState().dropped() - droppedBefore);
			*expectedLastCam = 3;
		});
		during("C12 observe", 1500, [this]() {
			std::string p = programUuid();
			if (p != lastProgramSeen_) {
				programChanges_++;
				lastProgramSeen_ = p;
			}
		});
		add("C12 verify", [this]() {
			check("C12", "20 rapid CUT requests produce exactly one take (no backlog of stale takes)",
			      repeatAccepted_ == 1 && programChanges_ == 1 && programIs(3),
			      QString("accepted=%1 program changes=%2").arg(repeatAccepted_).arg(programChanges_));
		});

		// ---- Keyboard shortcuts and typing suppression
		add("C13 shortcuts", [this]() {
			sendKey(Qt::Key_5, false, "5");
			bool k5 = previewUuid() == sceneUuid(4);
			std::string before = previewUuid();
			sendKey(Qt::Key_6, true, "6"); // auto-repeat must be ignored
			bool repeatIgnored = previewUuid() == before;
			// Typing in a text field: open Setup, focus the URL field, then press 7.
			ctl()->window()->openSetup();
			QLineEdit *edit = nullptr;
			for (auto *e : ctl()->window()->findChildren<QLineEdit *>())
				if (e->echoMode() == QLineEdit::Password) {
					edit = e;
					break;
				}
			bool focused = false;
			if (edit) {
				edit->window()->activateWindow();
				edit->setFocus(Qt::OtherFocusReason);
				QApplication::processEvents();
				focused = QApplication::focusWidget() == edit;
			}
			// Deliver the keystroke the way Qt would: to the focused widget.
			QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : (QWidget *)edit;
			QString textBefore = edit ? edit->text() : QString();
			if (target) {
				QKeyEvent ev(QEvent::KeyPress, Qt::Key_7, Qt::NoModifier, "7");
				QCoreApplication::sendEvent(target, &ev);
			}
			bool typed = edit && edit->text() == textBefore + "7";
			if (edit)
				edit->setText(textBefore);
			bool suppressed = previewUuid() == before;
			check("C13", "Number keys select Preview; held-key auto-repeat is ignored",
			      k5 && repeatIgnored);
			check("C14", "Typing in a URL field never selects/takes a camera", typed && suppressed,
			      QString("focus on URL field=%1, key reached field=%2, preview unchanged=%3")
				      .arg(focused)
				      .arg(typed)
				      .arg(suppressed));
			for (auto *w : QApplication::topLevelWidgets())
				if (w->windowTitle().contains("Setup"))
					w->close();
			ctl()->window()->activateWindow();
			ctl()->window()->setFocus();
			QApplication::processEvents();
		});

		// ---- Graphics visibility
		add("C15 graphics toggle", [this]() {
			OBSSourceAutoRelease g = ctl()->bank().cameraGraphics();
			std::string gUuid = uuidOf(g);
			OBSDataAutoRelease before = obs_source_get_settings(g);
			std::string beforeJson = obs_data_get_json(before);
			ctl()->setGraphicsVisible(false);
			bool allOff = ctl()->graphics().visibility() == GraphicsVisibility::Off;
			// Program instance (Studio Mode may run a duplicate scene on Program)
			OBSSourceAutoRelease tr = obs_get_output_source(0);
			OBSSourceAutoRelease active = obs_transition_get_active_source(tr);
			bool programOff = true;
			for (auto *it : itemsWithSource(obs_scene_from_source(active), g))
				programOff = programOff && !obs_sceneitem_visible(it);
			ctl()->setGraphicsVisible(true);
			bool allOn = ctl()->graphics().visibility() == GraphicsVisibility::On;
			OBSSourceAutoRelease g2 = ctl()->bank().cameraGraphics();
			OBSDataAutoRelease after = obs_source_get_settings(g2);
			bool sameSource = uuidOf(g2) == gUuid && beforeJson == obs_data_get_json(after);
			check("C15",
			      "One toggle hides/shows the owned graphics item in all eight camera scenes (and the Program "
			      "instance); the source itself is untouched",
			      allOff && programOff && allOn && sameSource,
			      QString("off=%1 programOff=%2 on=%3 sourceUnchanged=%4 duplicateSceneMode=%5")
				      .arg(allOff)
				      .arg(programOff)
				      .arg(allOn)
				      .arg(sameSource)
				      .arg(studioSceneDuplicationEnabled()));
			// Manual edit leaves mixed visibility
			OBSSourceAutoRelease s6 = ctl()->bank().slotScene(5);
			obs_sceneitem_set_visible(ctl()->bank().graphicsItem(5, obs_scene_from_source(s6), g), false);
			bool mixed = ctl()->graphics().visibility() == GraphicsVisibility::Mixed;
			ctl()->toggleGraphics();
			bool resolved = ctl()->graphics().visibility() == GraphicsVisibility::On;
			check("C16", "Manual per-scene edits show MIXED until a global setting is applied",
			      mixed && resolved);
		});

		// ---- Replay / Highlights loading
		add(
			"load replay + highlights",
			[this]() {
				replayLoadOk_ = ctl()->loadPlayback(kSlotReplay, mediaPath("replay.mp4"));
				highlightsLoadOk_ = ctl()->loadPlayback(kSlotHighlights, mediaPath("highlights.mp4"));
				mark("playback_loaded");
			},
			[this]() {
				return pbState(kSlotReplay) == PlaybackState::Ready &&
				       pbState(kSlotHighlights) == PlaybackState::Ready;
			},
			12000);
		add("C17 verify arm", [this]() {
			OBSSourceAutoRelease r = media(kSlotReplay), h = media(kSlotHighlights);
			bool ok = replayLoadOk_ && highlightsLoadOk_ && pbState(kSlotReplay) == PlaybackState::Ready &&
				  obs_source_media_get_state(r) == OBS_MEDIA_STATE_PAUSED &&
				  mediaTime(kSlotReplay) < 200 && !obs_source_active(r) && !obs_source_active(h) &&
				  obs_source_get_width(r) > 0;
			check("C17", "Loaded files arm paused at the first frame (READY), not active in the output", ok,
			      QString("replay t=%1ms state=%2 highlights t=%3ms")
				      .arg(mediaTime(kSlotReplay))
				      .arg(obs_source_media_get_state(r))
				      .arg(mediaTime(kSlotHighlights)));
		});

		// ---- Off-air: Preview wait does not consume the clip
		add("stage replay in preview", [this]() {
			ctl()->selectSlot(kSlotReplay);
			offAirMaxTime_ = 0;
			offAirActive_ = false;
			startedBefore_ = g_replayStarted;
			mark("offair_wait_start");
		});
		during(QString("C18 replay in Preview for %1 s").arg(offAirSeconds_), offAirSeconds_ * 1000, [this]() {
			OBSSourceAutoRelease r = media(kSlotReplay);
			offAirMaxTime_ = std::max(offAirMaxTime_, mediaTime(kSlotReplay));
			offAirActive_ = offAirActive_ || obs_source_active(r);
		});
		add("C18 verify", [this]() {
			mark("offair_wait_end");
			check("C18",
			      QString("Clip left in its tile and Preview for %1 s does not advance and is not active "
				      "(no audio to Program)")
				      .arg(offAirSeconds_),
			      offAirMaxTime_ < 200 && !offAirActive_ && pbState(kSlotReplay) == PlaybackState::Ready &&
				      previewUuid() == sceneUuid(kSlotReplay) && g_replayStarted == startedBefore_,
			      QString("max media time %1 ms, active=%2").arg(offAirMaxTime_).arg(offAirActive_));
		});

		// ---- Take replay
		add(
			"C19 take replay",
			[this]() {
				startedBefore_ = g_replayStarted;
				firstLiveTime_ = -1;
				takeMs_ = nowMs();
				ctl()->cut();
				mark("replay_on_program");
			},
			[this]() {
				OBSSourceAutoRelease m = media(kSlotReplay);
				if (programIs(kSlotReplay) && firstLiveTime_ < 0 &&
				    obs_source_media_get_state(m) == OBS_MEDIA_STATE_PLAYING) {
					firstLiveTime_ = mediaTime(kSlotReplay);
					firstLiveAt_ = nowMs() - takeMs_;
				}
				return firstLiveTime_ >= 0;
			},
			3000);
		add("C19 verify", [this, expectedLastCam]() {
			OBSSourceAutoRelease r = media(kSlotReplay);
			check("C19", "CUT takes the armed replay to Program and starts it from the beginning",
			      programIs(kSlotReplay) && obs_source_active(r) && firstLiveTime_ <= firstLiveAt_ + 250 &&
				      g_replayStarted - startedBefore_ == 1,
			      QString("media time %1 ms observed %2 ms after the take, starts=%3")
				      .arg(firstLiveTime_)
				      .arg(firstLiveAt_)
				      .arg(g_replayStarted - startedBefore_));
			lastTime_ = mediaTime(kSlotReplay);
			monotonic_ = true;
			startedBefore_ = g_replayStarted;
			(void)expectedLastCam;
		});
		// While live: preview changes / resize / full screen must not restart.
		add("disturb while playing", [this]() {
			ctl()->selectSlot(0);
			ctl()->window()->resize(1100, 700);
			ctl()->window()->toggleFullScreen();
		});
		during("C20 observe playback", 2500, [this]() {
			int64_t t = mediaTime(kSlotReplay);
			if (t + 50 < lastTime_)
				monotonic_ = false;
			lastTime_ = t;
		});
		add("C20 verify", [this]() {
			ctl()->window()->toggleFullScreen();
			check("C20", "Preview change, resize and full screen do not restart an already playing clip",
			      monotonic_ && g_replayStarted == startedBefore_ &&
				      pbState(kSlotReplay) == PlaybackState::Playing,
			      QString("t=%1ms").arg(mediaTime(kSlotReplay)));
			std::string before = ctl()->config().replay.filePath;
			bool loaded = ctl()->loadPlayback(kSlotReplay, mediaPath("highlights.mp4"));
			bool armed = ctl()->armPlayback(kSlotReplay);
			check("C21", "File replace / arm is blocked while the playback source is on Program",
			      !loaded && !armed && ctl()->config().replay.filePath == before, ctl()->lastMessage());
			// Replacing the off-air Highlights reel while Replay is live is allowed.
			bool hl = ctl()->loadPlayback(kSlotHighlights, mediaPath("highlights.mp4"));
			check("C22", "An off-air Highlights reel can be replaced while Replay is live",
			      hl && pbState(kSlotReplay) == PlaybackState::Playing);
		});
		waitFor(
			"highlights re-armed", [this]() { return pbState(kSlotHighlights) == PlaybackState::Ready; },
			10000);

		// ---- Replay -> Highlights -> Return Live
		add(
			"take highlights",
			[this]() {
				ctl()->selectSlot(kSlotHighlights);
				hlStartedBefore_ = g_highlightsStarted;
				ctl()->cut();
				mark("highlights_on_program");
			},
			[this]() {
				return programIs(kSlotHighlights) && pbState(kSlotHighlights) == PlaybackState::Playing;
			},
			4000);
		waitFor(
			"replay re-armed after leaving Program",
			[this]() { return pbState(kSlotReplay) == PlaybackState::Ready; }, 8000);
		add("C23 verify", [this, expectedLastCam]() {
			OBSSourceAutoRelease r = media(kSlotReplay);
			check("C23",
			      "Replay leaving Program is paused and re-armed at its first frame; Highlights plays",
			      pbState(kSlotReplay) == PlaybackState::Ready && mediaTime(kSlotReplay) < 200 &&
				      !obs_source_active(r) && g_highlightsStarted - hlStartedBefore_ == 1,
			      QString("replay t=%1").arg(mediaTime(kSlotReplay)));
			SwitchView sv = ctl()->switcher().view();
			check("C24",
			      "Last live camera is preserved across Replay ↔ Highlights (never the Preview identity)",
			      sv.lastCameraSlot == *expectedLastCam && sv.returnLiveEnabled,
			      QString("last camera slot %1 expected %2, preview slot %3")
				      .arg(sv.lastCameraSlot)
				      .arg(*expectedLastCam)
				      .arg(sv.previewSlot));
		});
		sleep(3000); // keep Highlights on air long enough for the audio analysis
		add(
			"return live",
			[this]() {
				ctl()->returnLive();
				mark("return_live");
			},
			[this, expectedLastCam]() { return programIs(*expectedLastCam); }, 3000);
		waitFor(
			"highlights re-armed", [this]() { return pbState(kSlotHighlights) == PlaybackState::Ready; },
			8000);
		add("C25 verify", [this, expectedLastCam]() {
			check("C25", "Return Live cuts to the last live camera; the playback slot re-arms",
			      programIs(*expectedLastCam) && pbState(kSlotHighlights) == PlaybackState::Ready &&
				      mediaTime(kSlotHighlights) < 200);
		});

		// ---- Native OBS takes (bypassing the plugin)
		add(
			"native take replay",
			[this]() {
				startedBefore_ = g_replayStarted;
				OBSSourceAutoRelease s = ctl()->bank().slotScene(kSlotReplay);
				obs_frontend_set_current_scene(s);
				mark("native_replay_on_program");
			},
			[this]() { return programIs(kSlotReplay) && pbState(kSlotReplay) == PlaybackState::Playing; },
			4000);
		sleep(2500);
		add("C26 verify", [this]() {
			check("C26", "A native OBS take into Replay starts it once on actual Program entry",
			      g_replayStarted - startedBefore_ == 1 && mediaTime(kSlotReplay) > 1500,
			      QString("starts=%1 t=%2")
				      .arg(g_replayStarted - startedBefore_)
				      .arg(mediaTime(kSlotReplay)));
		});
		// Let it run to the end: ENDED, no automatic switch.
		waitFor("replay ended", [this]() { return pbState(kSlotReplay) == PlaybackState::Ended; }, 12000);
		sleep(1500);
		add("C27 verify", [this]() {
			mark("replay_ended");
			check("C27", "At clip end: ENDED shown, last frame held, no loop and no automatic scene change",
			      pbState(kSlotReplay) == PlaybackState::Ended && programIs(kSlotReplay) &&
				      g_replayStarted - startedBefore_ == 1,
			      QString("t=%1/%2")
				      .arg(mediaTime(kSlotReplay))
				      .arg(ctl()->playback().view(kSlotReplay).durationMs));
		});
		add(
			"native take back to camera",
			[this, expectedLastCam]() {
				OBSSourceAutoRelease s = ctl()->bank().slotScene(0);
				obs_frontend_set_current_scene(s);
				*expectedLastCam = 0;
				mark("native_camera");
			},
			[this]() { return programIs(0); }, 3000);
		waitFor(
			"replay re-armed after ENDED",
			[this]() { return pbState(kSlotReplay) == PlaybackState::Ready; }, 8000);
		add("C28 verify", [this]() {
			SwitchView sv = ctl()->switcher().view();
			check("C28", "Native OBS scene changes update Program, tally and last-camera truthfully",
			      sv.programSlot == 0 && sv.lastCameraSlot == 0 && mediaTime(kSlotReplay) < 200);
		});

		// ---- External transition in progress
		add(
			"native fade",
			[this]() {
				OBSSourceAutoRelease fade = findTransitionByName("Fade");
				if (fade)
					obs_frontend_set_current_transition(fade);
				obs_frontend_set_transition_duration(1500);
				OBSSourceAutoRelease s = ctl()->bank().slotScene(1);
				obs_frontend_set_current_scene(s);
			},
			[]() { return transitionInProgress(); }, 3000);
		add("C29 verify", [this]() {
			ctl()->selectSlot(2);
			bool rejected = !ctl()->cut();
			check("C29", "A plugin take is suspended while an external transition is running",
			      rejected && ctl()->lastMessage().contains("transition"), ctl()->lastMessage());
		});
		waitFor("fade done", [this]() { return !transitionInProgress() && programIs(1); }, 5000);
		cutTo(2, "CAM 3 after fade");
		add("C30 verify", [this, expectedLastCam]() {
			*expectedLastCam = 2;
			OBSSourceAutoRelease tr = obs_frontend_get_current_transition();
			check("C30", "After an external fade, plugin CUT re-selects Cut and takes normally",
			      programIs(2) && strcmp(obs_source_get_unversioned_id(tr), "cut_transition") == 0);
		});

		// ---- Failure handling
		add(
			"C31 missing media",
			[this]() {
				bool ok = ctl()->loadPlayback(kSlotHighlights, "/nonexistent/reel.mp4");
				bool err = pbState(kSlotHighlights) == PlaybackState::Error;
				ctl()->selectSlot(kSlotHighlights);
				bool previewRejected = previewUuid() != sceneUuid(kSlotHighlights);
				ctl()->selectSlot(3);
				bool camOk = ctl()->cut();
				check("C31",
				      "Missing media errors only that playback slot; camera switching still works",
				      !ok && err && previewRejected && camOk, ctl()->lastMessage());
			},
			[this]() { return programIs(3); }, 3000);
		add(
			"reload highlights",
			[this]() { ctl()->loadPlayback(kSlotHighlights, mediaPath("highlights.mp4")); },
			[this]() { return pbState(kSlotHighlights) == PlaybackState::Ready; }, 10000);

		add(
			"take camera 8",
			[this]() {
				ctl()->selectSlot(7);
				ctl()->cut();
			},
			[this]() { return programIs(7) && !ctl()->switcher().takeState().busy(); }, 3000);
		add(
			"take replay, then delete camera 8's input",
			[this]() {
				ctl()->selectSlot(kSlotReplay);
				ctl()->cut();
			},
			[this]() { return programIs(kSlotReplay) && !ctl()->switcher().takeState().busy(); }, 3000);
		add("C32 deleted input", [this]() {
			OBSSourceAutoRelease in8 = ctl()->bank().cameraInput(7);
			obs_source_remove(in8);
			in8 = nullptr;
			parked_.erase(parked_.begin() + 7);
			QApplication::processEvents();
			SlotFacts f = ctl()->switcher().slotFacts(7);
			bool sel = ctl()->selectSlot(7);
			SwitchView sv = ctl()->switcher().view();
			check("C32",
			      "Deleted camera input: slot stays visible as MISSING, cannot be selected; Return Live to it "
			      "is disabled (no random replacement); no automatic take",
			      f.sceneExists && f.inputAssigned && !f.inputExists && !sel && !sv.returnLiveEnabled &&
				      programIs(kSlotReplay),
			      QString::fromStdString(sv.returnLiveReason));
		});
		cutTo(0, "CAM 1 after deletion");
		waitFor("replay re-armed", [this]() { return pbState(kSlotReplay) == PlaybackState::Ready; }, 8000);
		add(
			"C33 deleted scene",
			[this]() {
				ctl()->selectSlot(kSlotReplay);
				ctl()->cut();
			},
			[this]() { return programIs(kSlotReplay) && !ctl()->switcher().takeState().busy(); }, 3000);
		add("C33 verify", [this]() {
			// Delete CAM 1's scene (the last live camera) while Replay is on air.
			OBSSourceAutoRelease s1 = ctl()->bank().slotScene(0);
			std::string oldUuid = uuidOf(s1);
			obs_source_remove(s1);
			s1 = nullptr;
			QApplication::processEvents();
			SwitchView sv = ctl()->switcher().view();
			deletedSceneUuid_ = oldUuid;
			check("C33",
			      "Deleting the last-live camera scene disables Return Live (no random replacement, no "
			      "automatic take)",
			      !sv.returnLiveEnabled && programIs(kSlotReplay) && sceneUuid(0).empty(),
			      QString::fromStdString(sv.returnLiveReason));
		});
		cutTo(1, "CAM 2 after scene deletion");

		// ---- Rename
		add("C35 rename", [this]() {
			OBSSourceAutoRelease s = ctl()->bank().slotScene(2);
			std::string uuid = uuidOf(s);
			obs_source_set_name(s, "Renamed Left Goal");
			bool sel = ctl()->selectSlot(2);
			ctl()->bank().refreshNames();
			check("C35", "Renaming a bank scene keeps its assignment and identity",
			      sel && sceneUuid(2) == uuid && previewUuid() == uuid &&
				      ctl()->config().cameras[2].sceneName == "Renamed Left Goal");
		});

		// ---- Window lifecycle
		add("C36 window lifecycle", [this]() {
			MultiviewWindow *w = ctl()->window();
			for (int i = 0; i < 10; i++) {
				w->close();
				QApplication::processEvents();
				ctl()->showMultiview();
				QApplication::processEvents();
			}
			const QSize sizes[] = {QSize(960, 640), QSize(1920, 1080), QSize(1280, 720), QSize(1500, 1000)};
			for (auto s : sizes) {
				w->resize(s);
				QApplication::processEvents();
			}
			framesBefore_ = w->surface()->framesDrawn();
		});
		sleep(1500);
		add("C36 verify", [this]() {
			MultiviewWindow *w = ctl()->window();
			bool drawing = w->surface()->framesDrawn() > framesBefore_ && w->surface()->displayValid();
			// Showing balance: after closing, scenes not on Preview/Program are no longer showing.
			w->close();
			QApplication::processEvents();
			w->surface()->purgeGraveyard();
			int showing = 0;
			for (int i = 0; i < 10; i++) {
				if (sceneUuid(i) == previewUuid() || sceneUuid(i) == programUuid())
					continue;
				OBSSourceAutoRelease s = ctl()->bank().slotScene(i);
				showing += obs_source_showing(s) ? 1 : 0;
			}
			bool outputsOk = obs_frontend_recording_active() && !programUuid().empty();
			check("C36", "Repeated open/close and resize: display renders, references released on close",
			      drawing && showing == 0,
			      QString("frames drawn while open: %1; scenes still showing after close: %2")
				      .arg(w->surface()->framesDrawn() - framesBefore_)
				      .arg(showing));
			check("C37", "Closing the multiview leaves Program and recording running", outputsOk);
			ctl()->showMultiview();
		});

		// ---- Performance: multiview open vs closed
		addPerfWindow("open");
		add("close for perf", [this]() { ctl()->window()->close(); });
		addPerfWindow("closed");
		add("reopen", [this]() { ctl()->showMultiview(); });

		if (soakSeconds_ > 0)
			addSoak();

		add(
			"stop recording", []() { obs_frontend_recording_stop(); },
			[]() { return !obs_frontend_recording_active(); }, 15000);
		add(
			"C33b repair after recording",
			[this]() {
				ctl()->selectSlot(kSlotReplay);
				ctl()->cut();
			},
			[this]() { return programIs(kSlotReplay) && !ctl()->switcher().takeState().busy(); }, 3000);
		add("C33b verify", [this]() {
			ctl()->switcher().tracker().reset(deletedSceneUuid_, 0); // last live = the deleted scene
			RepairReport r = ctl()->createOrRepair();
			SwitchView sv = ctl()->switcher().view();
			bool recreated = !sceneUuid(0).empty() && sceneUuid(0) != deletedSceneUuid_;
			check("C33b",
			      "Repair recreates the deleted scene with a new identity; Return Live still refuses the "
			      "old identity",
			      recreated && r.created > 0 && !sv.returnLiveEnabled && countOwnedScenes() == 10,
			      QString::fromStdString(sv.returnLiveReason));
			check("C34", "Create / Repair never switches Program", programIs(kSlotReplay));
		});
		cutTo(1, "CAM 2 at end");
		add("recording path", [this]() {
			char *p = obs_frontend_get_last_recording();
			recordingPath_ = p ? p : "";
			bfree(p);
			mark("recording_stopped");
		});
		add("save collection", []() { obs_frontend_save(); });
	}

	void addPerfWindow(const QString &label)
	{
		auto st = std::make_shared<std::map<std::string, double>>();
		auto cpu = std::make_shared<os_cpu_usage_info_t *>(nullptr);
		auto samples = std::make_shared<std::vector<double>>();
		add("perf start " + label, [st, cpu, samples]() {
			(*st)["lagged"] = obs_get_lagged_frames();
			(*st)["total"] = obs_get_total_frames();
			(*st)["skipped"] = video_output_get_skipped_frames(obs_get_video());
			(*st)["vtotal"] = video_output_get_total_frames(obs_get_video());
			(*st)["t"] = (double)nowMs();
			*cpu = os_cpu_usage_info_start();
			samples->clear();
		});
		auto lastSample = std::make_shared<int64_t>(0);
		during("perf " + label, perfSeconds_ * 1000, [cpu, samples, lastSample]() {
			if (nowMs() - *lastSample >= 1000) {
				*lastSample = nowMs();
				samples->push_back(os_cpu_usage_info_query(*cpu));
			}
		});
		add("perf end " + label, [this, st, cpu, samples, label]() {
			QJsonObject o;
			o["seconds"] = (nowMs() - (*st)["t"]) / 1000.0;
			o["render_frames"] = obs_get_total_frames() - (*st)["total"];
			o["render_lagged_frames"] = obs_get_lagged_frames() - (*st)["lagged"];
			o["encode_frames"] = video_output_get_total_frames(obs_get_video()) - (*st)["vtotal"];
			o["encode_skipped_frames"] =
				video_output_get_skipped_frames(obs_get_video()) - (*st)["skipped"];
			double sum = 0;
			for (double v : *samples)
				sum += v;
			o["cpu_percent_avg"] = samples->empty() ? 0.0 : sum / samples->size();
			o["average_frame_render_ms"] = obs_get_average_frame_time_ns() / 1e6;
			if (*cpu)
				os_cpu_usage_info_destroy(*cpu);
			perf_[label] = o;
			mark("perf_" + label);
		});
	}

	void addSoak()
	{
		auto next = std::make_shared<int64_t>(0);
		auto step = std::make_shared<int>(0);
		auto takes = std::make_shared<int>(0);
		auto failures = std::make_shared<int>(0);
		auto lagStart = std::make_shared<double>(0);
		add("soak start", [lagStart]() { *lagStart = obs_get_lagged_frames(); });
		during(QString("soak %1 s").arg(soakSeconds_), soakSeconds_ * 1000,
		       [this, next, step, takes, failures]() {
			       if (nowMs() < *next || ctl()->switcher().takeState().busy())
				       return;
			       *next = nowMs() + 4000;
			       int s = (*step)++ % 12;
			       bool ok = true;
			       if (s < 8) {
				       int cam = s == 7 ? 0 : s; // camera 8's input was deleted earlier
				       ctl()->selectSlot(cam);
				       ok = ctl()->cut();
			       } else if (s == 8 || s == 10) {
				       int slot = s == 8 ? kSlotReplay : kSlotHighlights;
				       if (pbState(slot) == PlaybackState::Ready) {
					       ctl()->selectSlot(slot);
					       ok = ctl()->cut();
				       }
			       } else {
				       ok = ctl()->returnLive() || ctl()->lastMessage().contains("already");
			       }
			       (*takes)++;
			       if (!ok)
				       (*failures)++;
		       });
		add("soak verify", [this, takes, failures, lagStart]() {
			QJsonObject o;
			o["seconds"] = soakSeconds_;
			o["take_attempts"] = *takes;
			o["take_rejections"] = *failures;
			o["render_lagged_frames"] = obs_get_lagged_frames() - *lagStart;
			perf_["soak"] = o;
			check("C38", QString("Soak: %1 s of repeated camera / playback takes").arg(soakSeconds_),
			      *takes > 0, QString("%1 takes, %2 rejected").arg(*takes).arg(*failures));
		});
	}

	// ------------------------------------------------------------------ phase 2
	// ------------------------------------------------------------------ phase 3
	// Replay Cue provider, against the stand-in "ReplayCue" vendor (selftest-fake-replaycue.cpp).
	std::string programName()
	{
		OBSSourceAutoRelease p = obs_frontend_get_current_scene();
		return nameOf(p);
	}
	std::string previewName()
	{
		OBSSourceAutoRelease p = obs_frontend_get_current_preview_scene();
		return nameOf(p);
	}

	void buildPhase3()
	{
		auto cams = std::make_shared<std::vector<std::string>>();
		add(
			"studio mode", []() { obs_frontend_set_preview_program_mode(true); },
			[]() { return obs_frontend_preview_program_mode_active(); });
		add("create cameras", [this, cams]() {
			obs_scene_t *park = obs_scene_create("Unrelated Test Inputs");
			for (int i = 1; i <= 2; i++) {
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_string(s, "local_file",
						    mediaPath(QString("cam%1.mp4").arg(i)).toUtf8().constData());
				obs_data_set_bool(s, "looping", true);
				obs_data_set_bool(s, "restart_on_activate", false);
				OBSSourceAutoRelease src =
					createInput("ffmpeg_source", QString("TEST CAM %1").arg(i), s);
				obs_scene_add(park, src);
				cams->push_back(uuidOf(src));
			}
			obs_scene_release(park);
			QString m;
			ctl()->assignInput(0, (*cams)[0], &m);
			ctl()->assignInput(1, (*cams)[1], &m);
			ctl()->createOrRepair();
			ctl()->showMultiview();
		});
		cutTo(0, "CAM 1");

		add("R01 detect", [this]() {
			ctl()->setPlaybackProvider(true);
			ctl()->replayCue().refresh(true);
			const ReplayCueStatus &s = ctl()->replayCue().status();
			auto names = ctl()->replayCueScenes();
			check("R01", "Replay Cue detected in-process through obs-websocket (GetStatus / GetEvents)",
			      s.ok && s.armed && s.eventCount == 2 && s.phaseName == "Half Time" && names.first.empty(),
			      QString("ok=%1 events=%2 replay scene='%3' (not created until first play)")
				      .arg(s.ok)
				      .arg(s.eventCount)
				      .arg(QString::fromStdString(names.first)));
		});

		add(
			"R02 cue with 9", [this]() { sendKey(Qt::Key_9, false, "9"); },
			[this]() { return previewName() == "Replay"; }, 4000);
		add("R02 verify", [this]() {
			auto names = ctl()->replayCueScenes();
			check("R02",
			      "Key 9 asks Replay Cue to cue the selected replay; its Replay scene appears in Preview",
			      fakerc::calls("CueEvent") == 1 && names.first == "Replay" && programIs(0),
			      QString("CueEvent calls=%1, preview='%2', program still CAM 1=%3")
				      .arg(fakerc::calls("CueEvent"))
				      .arg(QString::fromStdString(previewName()))
				      .arg(programIs(0)));
		});

		add("R03 CUT", [this]() { ctl()->cut(); }, [this]() { return programName() == "Replay"; }, 4000);
		add("R03 verify", [this]() {
			SwitchView sv = ctl()->switcher().view();
			check("R03", "CUT on the cued replay asks Replay Cue to play it (Resume); last camera kept",
			      fakerc::calls("Resume") == 1 && sv.lastCameraSlot == 0 && sv.returnLiveEnabled,
			      QString("Resume calls=%1 last camera slot=%2")
				      .arg(fakerc::calls("Resume"))
				      .arg(sv.lastCameraSlot));
		});
		add("R04 Return Live", [this]() { ctl()->returnLive(); }, [this]() { return programIs(0); }, 4000);
		add("R04 verify", [this]() {
			check("R04",
			      "Return Live asks Replay Cue to return (ReturnToLive) and Program goes back to CAM 1",
			      fakerc::calls("ReturnToLive") == 1 && programIs(0));
		});

		add(
			"R05 setup fallback",
			[this]() {
				fakerc::setReturnWorks(false);
				ctl()->selectSlot(kSlotReplay);
			},
			[this]() { return previewName() == "Replay"; }, 4000);
		add("R05 take", [this]() { ctl()->cut(); }, [this]() { return programName() == "Replay"; }, 4000);
		sleep(1700); // past the CUT debounce
		add("R05 Return Live", [this]() { ctl()->returnLive(); }, [this]() { return programIs(0); }, 6000);
		add("R05 verify", [this]() {
			check("R05", "If Replay Cue cannot return, the multiview cuts to the last live camera itself",
			      fakerc::calls("ReturnToLive") == 2 && programIs(0) &&
				      ctl()->lastMessage().contains("did not return"),
			      ctl()->lastMessage());
			fakerc::setReturnWorks(true);
		});

		add(
			"R06 stage highlights", [this]() { sendKey(Qt::Key_0, false, "0"); },
			[this]() { return previewName() == "Replay" && ctl()->replayCueHighlightsStaged(); }, 4000);
		add(
			"R06 CUT twice",
			[this]() {
				ctl()->cut();
				ctl()->cut(); // double press must not restart the reel
			},
			[this]() { return programName() == "Replay"; }, 4000);
		add("R06 verify", [this]() {
			check("R06",
			      "Key 0 stages the highlights reel; CUT plays it once (PlayHighlights), double press ignored",
			      fakerc::calls("PlayHighlights") == 1 && fakerc::highlightsPlaying() &&
				      fakerc::calls("Resume") == 2,
			      QString("PlayHighlights calls=%1").arg(fakerc::calls("PlayHighlights")));
		});
		sleep(1700);
		add("R06 return", [this]() { ctl()->returnLive(); }, [this]() { return programIs(0); }, 4000);

		add("R07 panel controls", [this]() {
			OBSDataAutoRelease d = obs_data_create();
			obs_data_set_bool(d, "play", false);
			bool a = ctl()->replayCueAction("InstantReplay", d);
			OBSDataAutoRelease sp = obs_data_create();
			obs_data_set_double(sp, "percent", 50);
			bool b = ctl()->replayCueAction("SetSpeed", sp);
			bool c = ctl()->replayCueSelectRelative(-1);
			bool e = ctl()->replayCueAction("MarkIn") && ctl()->replayCueAction("MarkOut") &&
				 ctl()->replayCueAction("NextAngle");
			check("R07",
			      "Multiview controls drive Replay Cue: Instant Replay, 50% speed, previous replay, marks, angle",
			      a && b && c && e && fakerc::calls("InstantReplay") == 1 && fakerc::speed() == 0.5 &&
				      fakerc::selected() == "e1" && fakerc::calls("MarkIn") == 1 &&
				      fakerc::calls("NextAngle") == 1,
			      QString("selected=%1 speed=%2")
				      .arg(QString::fromStdString(fakerc::selected()))
				      .arg(fakerc::speed()));
			bool bad = ctl()->replayCueAction("NoSuchRequest");
			check("R07b", "An unknown / failing Replay Cue request is reported, not ignored",
			      !bad && ctl()->lastMessage().contains("Replay Cue"), ctl()->lastMessage());
		});

		cutTo(1, "CAM 2 in Replay Cue mode");
		add("R08 verify",
		    [this]() { check("R08", "Camera Preview / CUT is unchanged in Replay Cue mode", programIs(1)); });

		add("R09 built-in provider", [this]() {
			int before = fakerc::calls("CueEvent");
			ctl()->setPlaybackProvider(false);
			ctl()->selectSlot(kSlotReplay);
			check("R09", "With the built-in provider, 9 never calls Replay Cue",
			      fakerc::calls("CueEvent") == before && ctl()->lastMessage().contains("not READY"),
			      ctl()->lastMessage());
			ctl()->setPlaybackProvider(true);
		});
		sleep(1500); // let the multiview draw Replay Cue tiles for the screenshot
	}

	void buildPhase2()
	{
		add("P2 persistence", [this]() {
			obs_frontend_set_preview_program_mode(true);
			int owned = countOwnedScenes();
			BankConfig &c = ctl()->config();
			bool inputs = true;
			for (int i = 0; i < 7; i++) {
				OBSSourceAutoRelease in = ctl()->bank().cameraInput(i);
				inputs = inputs && in && uuidOf(in) == c.cameras[i].inputUuid;
			}
			OBSSourceAutoRelease s3 = ctl()->bank().slotScene(2);
			check("P01", "After restart: ten owned scenes and all camera assignments resolve by UUID",
			      owned == 10 && inputs, QString("owned=%1").arg(owned));
			check("P02", "After restart: renamed scene keeps its slot", nameOf(s3) == "Renamed Left Goal");
			check("P03", "After restart: labels, graphics URL and playback files restored",
			      c.cameras[0].label == "Main Wide" && !c.graphicsUrl.empty() &&
				      c.replay.filePath.find("replay.mp4") != std::string::npos);
			SwitchView sv = ctl()->switcher().view();
			check("P04", "After restart: last live camera identity restored", sv.lastCameraSlot >= 0,
			      QString("slot %1").arg(sv.lastCameraSlot));
			bankScene0_ = sceneUuid(0);
		});
		waitFor(
			"P05 playback re-armed after restart",
			[this]() {
				return pbState(kSlotReplay) == PlaybackState::Ready &&
				       pbState(kSlotHighlights) == PlaybackState::Ready;
			},
			15000);
		add("P05 verify", [this]() {
			check("P05",
			      "After restart: saved replay / highlights files are re-armed paused at the first frame",
			      pbState(kSlotReplay) == PlaybackState::Ready && mediaTime(kSlotReplay) < 200 &&
				      pbState(kSlotHighlights) == PlaybackState::Ready);
			ctl()->showMultiview();
		});
		add(
			"switch collection",
			[this]() {
				char *cur = obs_frontend_get_current_scene_collection();
				originalCollection_ = cur ? cur : "";
				bfree(cur);
				collectionChanges_ = 0;
				obs_frontend_add_scene_collection("MF Selftest Other");
			},
			[this]() { return collectionChanges_ >= 1 && ctl()->collectionReady(); }, 15000);
		sleep(1000);
		add("P06 verify", [this]() {
			bool none = !ctl()->bank().anySceneExists();
			bool selRejected = !ctl()->selectSlot(0);
			check("P06",
			      "Collection switch: the other collection has its own (empty) bank; no stale references used",
			      none && selRejected && countOwnedScenes() == 0, ctl()->lastMessage());
			collectionChanges_ = 0;
			obs_frontend_set_current_scene_collection(originalCollection_.c_str());
		});
		waitFor(
			"back to original collection",
			[this]() { return collectionChanges_ >= 1 && ctl()->collectionReady(); }, 15000);
		sleep(1500);
		add("P07 verify", [this]() {
			bool sel = ctl()->selectSlot(1);
			check("P07", "Switching back restores the original bank and switching works",
			      countOwnedScenes() == 10 && sceneUuid(0) == bankScene0_ && sel &&
				      previewUuid() == sceneUuid(1));
		});
		waitFor(
			"P08 re-armed after collection switch",
			[this]() { return pbState(kSlotReplay) == PlaybackState::Ready; }, 15000);
		add("P08 verify", [this]() {
			check("P08", "Playback re-armed after the collection switch",
			      pbState(kSlotReplay) == PlaybackState::Ready);
		});
	}

	// ------------------------------------------------------------------ state
	QString out_, media_, recDir_;
	int phase_ = 1, perfSeconds_ = 30, soakSeconds_ = 0, offAirSeconds_ = 60;
	QTimer timer_;
	std::vector<Step> steps_;
	size_t idx_ = 0;
	bool stepStarted_ = false, busy_ = false;
	int64_t stepStartMs_ = 0, t0_ = 0, recStartMs_ = 0;
	QJsonArray results_, timeline_;
	QJsonObject perf_;
	QString recordingPath_;
	std::vector<OBSSourceAutoRelease> parked_;
	std::vector<std::string> repairLines_;
	int startCount_ = 0;
	int64_t cutStart_ = 0;
	int sceneChangedBefore_ = 0, programChanges_ = 0, repeatAccepted_ = 0, repeatDropped_ = 0;
	std::string lastProgramSeen_;
	bool replayLoadOk_ = false, highlightsLoadOk_ = false;
	int64_t offAirMaxTime_ = 0, firstLiveTime_ = -1, lastTime_ = 0, takeMs_ = 0, firstLiveAt_ = 0;
	bool offAirActive_ = false, monotonic_ = true;
	int startedBefore_ = 0, hlStartedBefore_ = 0;
	uint64_t framesBefore_ = 0;
	int collectionChanges_ = 0;
	std::string originalCollection_, bankScene0_, deletedSceneUuid_;
};

SelfTest *g_test = nullptr;

} // namespace

void selftestOnEvent(enum obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING && !g_test && getenv("MATCHFRAME_SELFTEST_OUT")) {
		g_test = new SelfTest();
		QTimer::singleShot(1500, []() {
			if (g_test)
				g_test->start();
		});
	}
	if (event == OBS_FRONTEND_EVENT_EXIT && g_test) {
		g_test->deleteLater();
		g_test = nullptr;
		return;
	}
	if (g_test)
		g_test->onEvent(event);
}

} // namespace mf
