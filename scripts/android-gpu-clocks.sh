#!/system/bin/sh
# Runs on the phone (adb shell sh /data/local/tmp/android-gpu-clocks.sh 180 /data/local/tmp/clocks.txt):
# samples the Adreno clock once a second, one line per sample in the format
# tools/android_log_summary.py --clocks reads: "YYYY-MM-DD HH:MM:SS <gpuclk Hz> <busy %> <thermal_pwrlevel>".
# KGSL overrides the sysfs folder (for testing on a PC).
kgsl=${KGSL:-/sys/class/kgsl/kgsl-3d0}
seconds=${1:-120}
out=${2:-/data/local/tmp/gpu-clocks.txt}

# The shell user may not read gpuclk (Android 16 on the S25 Ultra); devfreq's cur_freq is the same Hz.
clock_hz() {
  cat "$kgsl/gpuclk" 2>/dev/null || cat "$kgsl/devfreq/cur_freq"
}

sample() {
  busy=$(cut -d' ' -f1 "$kgsl/gpu_busy_percentage")
  echo "$(date '+%Y-%m-%d %H:%M:%S') $(clock_hz) $busy $(cat "$kgsl/thermal_pwrlevel")"
}

: > "$out"
i=0
while [ "$i" -lt "$seconds" ]; do
  sample >> "$out"
  i=$((i + 1))
  sleep 1
done
