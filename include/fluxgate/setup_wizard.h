#pragma once
#include "fluxgate/config.h"
#include <string>

namespace fluxgate {

// Returns a populated AppConfig after interactively prompting the user.
// Writes the resulting config to `out_path` and optionally generates a CA.
AppConfig run_setup_wizard(const std::string& out_path = "./fluxgate.toml");

} // namespace fluxgate
