// MatchFrame Multiview - shared camera-graphics Browser Source control.
#pragma once

#include "scene-bank.hpp"

#include <string>

namespace mf {

enum class GraphicsVisibility { On, Off, Mixed, NotSetUp, Unavailable };

const char *graphicsVisibilityName(GraphicsVisibility v);

class GraphicsController {
public:
	explicit GraphicsController(SceneBank &bank) : bank_(bank) {}

	bool browserSupported() const { return bank_.browserAvailable(); }

	// Aggregate of the eight owned items (MIXED when manual edits disagree).
	GraphicsVisibility visibility(int *itemCount = nullptr) const;

	// One logical control: sets every owned item in the eight camera scenes, plus any
	// item of the shared browser inside the current Program scene instance (Studio Mode
	// "Duplicate Scene" puts a private copy of the scene on Program). Never touches the
	// browser source itself, so the page stays loaded.
	int setVisible(bool visible);

	// Explicit Apply URL: changes the page URL (this reloads the page by design).
	bool applyUrl(const std::string &url, std::string *message);

	// Playback overlays are independent of the camera toggle.
	bool setPlaybackOverlay(int slot, bool enabled, const std::string &url, std::string *message);

private:
	SceneBank &bank_;
};

} // namespace mf
