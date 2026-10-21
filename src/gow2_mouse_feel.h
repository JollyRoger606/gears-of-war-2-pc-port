// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Mouse feel: the weight the original game has with a stick, for the native mouse look. Pure arithmetic (header only, host tested);
// src/gow2_mouse_aim.cpp runs it on the banked counts of every input step.
//
//   1. curve      raw speed (counts per second) picks a gain: (speed / curve_ref) ^ (curve - 1). Above 1, slow movements are
//                 damped and fast ones amplified (fine aim, quick flicks); 1 is linear.
//   2. y_scale    vertical share of the horizontal movement.
//   3. smoothing  the counts go into a reservoir that empties exponentially, so slow movements are smoothed like a stick's small
//                 deflections. The time constant shrinks as the hand speeds up (it is smoothing_ms at rest and half of it at lag_ref
//                 counts per second), and what is left in the reservoir is released at once when the hand stops, so a flick is never
//                 chased afterwards. Nothing is lost.
//   4. max_speed  optional cap on how fast the reservoir may empty (counts per second): a flick cannot whip the camera round
//                 instantly. The backlog is limited to max_backlog_ms of that speed so the view never chases an old hand.
#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace rex::glue::mousefeel {

struct Params {
  double smoothing_ms = 0;     // 0 = none
  double curve = 1.0;          // 1 = linear
  double curve_ref = 1500;     // counts per second where the gain is 1
  double lag_ref = 600;        // counts per second at which the lag time constant has halved
  double max_speed = 0;        // counts per second, 0 = no cap
  double max_backlog_ms = 250;
  double y_scale = 1.0;
};

struct State {
  double rx = 0, ry = 0;  // counts not yet released
  double speed = 0;       // recent hand speed, counts per second (fast to rise, slower to fall)
  double still = 0;       // seconds without movement
};

struct Out {
  double dx = 0, dy = 0;
};

inline bool Neutral(const Params& p) {
  return p.smoothing_ms <= 0 && std::fabs(p.curve - 1.0) < 1e-9 && p.max_speed <= 0 && std::fabs(p.y_scale - 1.0) < 1e-9;
}

// One input step: (dx, dy) are the counts since the last step, dt the seconds since it.
inline Out Step(const Params& p, State& s, double dx, double dy, double dt) {
  dt = std::clamp(dt, 0.001, 0.1);
  const double raw_speed = std::hypot(dx, dy) / dt;
  s.speed = std::max(raw_speed, s.speed * std::exp(-dt / 0.06));
  s.still = (dx != 0 || dy != 0) ? 0.0 : s.still + dt;
  if ((dx != 0 || dy != 0) && std::fabs(p.curve - 1.0) > 1e-9) {
    const double speed = std::hypot(dx, dy) / dt;
    const double gain = std::clamp(std::pow(std::max(speed, 1.0) / std::max(p.curve_ref, 1.0), p.curve - 1.0), 0.15, 4.0);
    dx *= gain;
    dy *= gain;
  }
  dy *= p.y_scale;
  // The reservoir r empties at r / tau while this step's counts arrive evenly over dt: the exact solution, so the lag does not depend
  // on the step rate. out = r * f + in * (1 - tau / dt * f) with f = 1 - exp(-dt / tau).
  const double tau = p.smoothing_ms / 1000.0 / (1.0 + s.speed / std::max(p.lag_ref, 1.0));
  Out out{dx, dy};
  if (p.smoothing_ms > 0 && s.still < 0.03) {  // shortcut: after 30 ms without movement the rest comes out at once
    const double f = 1.0 - std::exp(-dt / tau);
    const double g = 1.0 - tau / dt * f;
    out = {s.rx * f + dx * g, s.ry * f + dy * g};
  } else {
    out = {s.rx + dx, s.ry + dy};
  }
  if (p.max_speed > 0) {
    const double lim = p.max_speed * dt;
    const double mag = std::hypot(out.dx, out.dy);
    if (mag > lim) {
      out.dx *= lim / mag;
      out.dy *= lim / mag;
    }
  }
  s.rx += dx - out.dx;
  s.ry += dy - out.dy;
  if (p.max_speed > 0) {  // shortcut: the backlog is cut, not stretched, when the hand outruns the cap for long
    const double limit = p.max_speed * p.max_backlog_ms / 1000.0;
    const double mag = std::hypot(s.rx, s.ry);
    if (mag > limit) {
      s.rx *= limit / mag;
      s.ry *= limit / mag;
    }
  }
  if (std::hypot(s.rx, s.ry) < 0.05) {  // do not leave an endless tail
    out.dx += s.rx;
    out.dy += s.ry;
    s.rx = s.ry = 0;
  }
  return out;
}

// Presets by name; anything else (including "custom") returns false and leaves p alone.
inline bool Preset(std::string_view name, Params& p) {
  if (name == "off") p = Params{};
  else if (name == "light") p = Params{20, 1.0, 1500, 600, 0, 250, 1.0};
  else if (name == "original") p = Params{45, 1.4, 1800, 600, 0, 250, 0.9};
  else if (name == "heavy") p = Params{80, 1.6, 2200, 500, 0, 250, 0.8};
  else return false;
  return true;
}

}  // namespace rex::glue::mousefeel
