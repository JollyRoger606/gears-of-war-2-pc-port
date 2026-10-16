// gow2 - ReXGlue Recompiled Project
//
// Polls gow2_dev_command_file for console commands and writes per-line results to <file>.ack.

#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_dev_host_verbs.h"
#include "gow2_engine_tick.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_STRING(gow2_dev_command_file, "", "Testing", "Dev only: a text file the game polls for console commands, one per line (format in src/gow2_dev_commands.h). Results go to <file>.ack and to DEVCMD log lines. Empty = off; needs a restart");

namespace rex::glue {
namespace {

struct PendingLine {
  size_t number;
  std::string text;
};

struct Pump {
  bool initialized = false;
  bool enabled = false;
  std::string path;
  std::string ack_path;
  size_t consumed = 0;
  std::chrono::steady_clock::time_point next_read{};
  std::deque<PendingLine> queue;
  bool front_started = false;
  std::chrono::steady_clock::time_point front_since{};
};

constexpr auto kReadEvery = std::chrono::milliseconds(100);
constexpr auto kTargetWait = std::chrono::seconds(30);
constexpr size_t kMaxFileBytes = 1024u * 1024u;

Pump& ThePump() {
  static Pump pump;
  return pump;
}

std::string ReadWholeFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool IsGameEngine(const uint8_t* base, uint32_t engine) {
  bool is_engine = false;
  auto check = [&] {
    is_engine =
        dev_commands::ClassChainHas(base, dev_commands::kGameGlobals, engine, "GameEngine");
  };
  if (!guest_call::Guarded(check)) return false;
  return is_engine;
}

std::string EngineClassName(const uint8_t* base, uint32_t engine) {
  std::string name = "?";
  auto read = [&] { name = dev_commands::ClassName(base, dev_commands::kGameGlobals, engine); };
  if (!guest_call::Guarded(read)) return "?";
  return name;
}

void InitializePump(const PPCContext& ctx, uint8_t* base, Pump* pump) {
  pump->initialized = true;
  pump->path = REXCVAR_GET(gow2_dev_command_file);
  if (pump->path.empty()) return;
  if (!IsGameEngine(base, ctx.r3.u32)) {
    REXLOG_WARN("DEVCMD off: the engine object is a {}, not a GameEngine",
                EngineClassName(base, ctx.r3.u32));
    return;
  }
  pump->ack_path = pump->path + ".ack";
  pump->consumed = dev_commands::CountCompleteLines(ReadWholeFile(pump->path));
  pump->enabled = true;
  REXLOG_INFO("DEVCMD on: commands from {}, results in {} (skipped {} earlier lines)", pump->path,
              pump->ack_path, pump->consumed);
}

void EnqueueNewLines(Pump* pump, const std::string& content) {
  size_t base = pump->consumed;
  dev_commands::FileLines result = dev_commands::TakeCompleteLines(content, base);
  if (result.truncated) {
    REXLOG_INFO("DEVCMD {} was truncated; reading it from line 1", pump->path);
    base = 0;
    result = dev_commands::TakeCompleteLines(content, 0);
  }
  for (size_t i = 0; i < result.lines.size(); ++i)
    pump->queue.push_back(PendingLine{base + i + 1, result.lines[i]});
  pump->consumed = result.consumed;
}

void PollFile(Pump* pump) {
  const auto now = std::chrono::steady_clock::now();
  if (now < pump->next_read) return;
  pump->next_read = now + kReadEvery;
  const std::string content = ReadWholeFile(pump->path);
  if (content.size() > kMaxFileBytes) {
    REXLOG_WARN("DEVCMD {} is over 1 MiB; commands stop", pump->path);
    pump->enabled = false;
    return;
  }
  EnqueueNewLines(pump, content);
}

void DropSkipLines(Pump* pump) {
  while (!pump->queue.empty()) {
    const dev_commands::Command cmd =
        dev_commands::ParseCommandLine(pump->queue.front().text);
    if (cmd.kind != dev_commands::CommandKind::kSkip) return;
    pump->queue.pop_front();
    pump->front_started = false;
  }
}

void AppendAck(const std::string& ack_path, size_t number, bool ok,
               const std::string& detail) {
  std::ofstream out(ack_path, std::ios::binary | std::ios::app);
  if (!out) return;
  out << dev_commands::FormatAck(number, ok, detail);
  out.flush();
  out.close();
}

void Finish(Pump* pump, const PendingLine& line, bool ok, const std::string& detail) {
  AppendAck(pump->ack_path, line.number, ok, detail);
  if (ok) {
    REXLOG_INFO("DEVCMD #{} '{}' -> ok: {}", line.number, line.text, detail);
  } else {
    REXLOG_WARN("DEVCMD #{} '{}' -> error: {}", line.number, line.text, detail);
  }
  pump->queue.pop_front();
  pump->front_started = false;
}

std::string SafeDescribeTargets(const uint8_t* base, const std::vector<uint32_t>& pcs) {
  std::string out;
  auto walk = [&] {
    out = dev_commands::DescribeTargets(base, dev_commands::kGameGlobals, pcs);
  };
  if (!guest_call::Guarded(walk)) return std::to_string(pcs.size()) + " player controller(s)";
  return out;
}

std::string JoinOutputs(const std::vector<std::string>& outputs) {
  if (outputs.empty()) return "";
  std::string joined;
  for (const std::string& part : outputs) {
    if (!joined.empty()) joined += "; ";
    joined += part;
  }
  return " output '" + joined + "'";
}

void SendConsoleToTargets(const PPCContext& ctx, uint8_t* base, Pump* pump,
                          const PendingLine& line, const dev_commands::Command& cmd,
                          const std::vector<uint32_t>& targets) {
  std::vector<std::string> outputs;
  for (uint32_t pc : targets) {
    const std::optional<std::string> out = guest_call::ConsoleCommand(ctx, base, pc, cmd.text);
    if (!out.has_value()) {
      Finish(pump, line, false, "guest call failed for " + SafeDescribeTargets(base, {pc}));
      return;
    }
    if (!out->empty()) outputs.push_back(*out);
  }
  Finish(pump, line, true,
         "sent to " + SafeDescribeTargets(base, targets) + JoinOutputs(outputs));
}

void WaitOrFailEmptyTargets(Pump* pump, const PendingLine& line,
                            const dev_commands::Command& cmd,
                            std::chrono::steady_clock::time_point now) {
  if (cmd.target.kind == dev_commands::TargetKind::kName) {
    Finish(pump, line, false, "no player controller named " + cmd.target.name);
    return;
  }
  if (now - pump->front_since < kTargetWait) return;
  Finish(pump, line, false,
         "no player controller matches " + dev_commands::TargetToken(cmd.target) + " after 30 s");
}

void RunConsoleLine(const PPCContext& ctx, uint8_t* base, Pump* pump, const PendingLine& line,
                    const dev_commands::Command& cmd, std::chrono::steady_clock::time_point now) {
  const uint32_t world_info = dev_verbs::GuardedWorldInfo(base);
  const std::optional<std::vector<uint32_t>> targets =
      dev_verbs::GuardedTargets(base, ctx.r3.u32, world_info, cmd.target);
  if (!targets.has_value()) {
    Finish(pump, line, false, "fault while reading guest objects");
    return;
  }
  if (targets->empty()) {
    WaitOrFailEmptyTargets(pump, line, cmd, now);
    return;
  }
  SendConsoleToTargets(ctx, base, pump, line, cmd, *targets);
}

void RunHostVerbLine(const PPCContext& ctx, uint8_t* base, Pump* pump, const PendingLine& line,
                     const dev_commands::Command& cmd) {
  const dev_verbs::VerbResult result = dev_verbs::RunHostVerb(ctx, base, ctx.r3.u32, cmd);
  Finish(pump, line, result.ok, result.detail);
}

void RunFrontLine(const PPCContext& ctx, uint8_t* base, Pump* pump) {
  const auto now = std::chrono::steady_clock::now();
  if (!pump->front_started) {
    pump->front_started = true;
    pump->front_since = now;
  }
  const PendingLine front = pump->queue.front();
  const dev_commands::Command cmd = dev_commands::ParseCommandLine(front.text);
  if (cmd.kind == dev_commands::CommandKind::kError) {
    Finish(pump, front, false, cmd.text);
    return;
  }
  if (cmd.kind == dev_commands::CommandKind::kHostVerb) {
    RunHostVerbLine(ctx, base, pump, front, cmd);
    return;
  }
  if (cmd.kind == dev_commands::CommandKind::kSkip) {
    pump->queue.pop_front();
    pump->front_started = false;
    return;
  }
  RunConsoleLine(ctx, base, pump, front, cmd, now);
}

void PumpDevCommands(const PPCContext& ctx, uint8_t* base) {
  Pump& pump = ThePump();
  if (!pump.initialized) {
    InitializePump(ctx, base, &pump);
    return;
  }
  if (!pump.enabled) return;
  PollFile(&pump);
  if (!pump.enabled) return;
  DropSkipLines(&pump);
  if (pump.queue.empty()) return;
  RunFrontLine(ctx, base, &pump);
}

[[maybe_unused]] const bool kRegistered = (AddEngineTickListener(&PumpDevCommands), true);

}  // namespace
}  // namespace rex::glue
