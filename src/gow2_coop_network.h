// gow2 - ReXGlue Recompiled Project
//
// Co-op settings that must be in place before the runtime starts.
#pragma once

namespace rex::glue {

// gow2_coop_players 3 or more turns System Link on (net_link_active), unless gow2.toml or the
// command line already chose it. Call from OnPreSetup, before the kernel reads its settings.
void EnableSystemLinkForCoop();

}  // namespace rex::glue
