// MatchFrame Multiview - the ten plugin-owned scenes, their sources and identities.
//
// All methods run on the UI thread. Nothing here caches raw OBS pointers across calls:
// every lookup resolves the persisted identity (UUID + ownership marker) on demand.
#pragma once

#include "core/config.hpp"
#include "core/switch-logic.hpp"
#include "obs-util.hpp"

#include <string>
#include <vector>

namespace mf {

struct InputChoice {
	std::string uuid;
	std::string name;
	std::string typeId;
};

struct RepairReport {
	std::vector<std::string> lines;
	int created = 0;
	int reused = 0;
	int warnings = 0;
	bool pageReloaded = false;
	void add(const std::string &s) { lines.push_back(s); }
	void warn(const std::string &s)
	{
		lines.push_back("WARNING: " + s);
		warnings++;
	}
};

class SceneBank {
public:
	explicit SceneBank(BankConfig &cfg) : cfg_(cfg) {}

	BankConfig &config() { return cfg_; }
	const BankConfig &config() const { return cfg_; }

	static std::string sceneRole(int slot);
	static std::string mediaRole(int slot);
	static std::string playbackGraphicsRole(int slot);
	static constexpr const char *kCameraGraphicsRole = "cam_graphics";

	// --- resolution (never creates anything) ---
	OBSSourceAutoRelease slotScene(int slot) const;
	OBSSourceAutoRelease cameraInput(int cam); // may re-link by name (controlled fallback)
	OBSSourceAutoRelease mediaSource(int slot) const;
	OBSSourceAutoRelease cameraGraphics() const;
	OBSSourceAutoRelease playbackGraphics(int slot) const;
	int slotOfScene(obs_source_t *scene) const; // 0..9 or -1
	int slotOfSceneUuid(const std::string &uuid) const;
	bool anySceneExists() const;

	// Scene items (pointers valid while the owning scene reference is held, UI thread).
	obs_sceneitem_t *cameraItem(int cam, obs_scene_t *scene, obs_source_t *input) const;
	obs_sceneitem_t *graphicsItem(int cam, obs_scene_t *scene, obs_source_t *browser) const;

	bool browserAvailable() const;
	bool mediaAvailable() const;

	// --- setup actions (structural; caller blocks them while outputs are active) ---
	RepairReport createOrRepair();
	bool assignInput(int cam, const std::string &inputUuid, std::string *message);
	bool applyFraming(int cam, Framing framing, std::string *message);

	std::vector<InputChoice> videoInputs() const;
	// Other camera slots assigned to the same input as `cam`.
	std::vector<int> slotsSharingInput(int cam) const;

	// Keeps cfg scene/input names in sync with OBS (renames never break identity).
	void refreshNames();

	// Settings applied to plugin-created sources.
	static void applyBrowserDefaults(obs_data_t *settings, const std::string &url, uint32_t w, uint32_t h);
	static void applyMediaDefaults(obs_data_t *settings);
	static void applyFramingToItem(obs_sceneitem_t *item, Framing framing);

private:
	OBSSourceAutoRelease resolveOrCreateScene(int slot, RepairReport &r);
	OBSSourceAutoRelease resolveOrCreateBrowser(const std::string &uuidIn, const std::string &role,
						    const std::string &baseName, const std::string &url,
						    RepairReport &r, std::string &uuidOut);
	OBSSourceAutoRelease resolveOrCreateMedia(int slot, RepairReport &r);
	void repairCameraScene(int cam, obs_source_t *browser, RepairReport &r);
	void repairPlaybackScene(int slot, RepairReport &r);

	BankConfig &cfg_;
};

} // namespace mf
