// Measures QtAudioOutput of OpenHU: feeds Android Auto like packets (timing replayed from a
// protocol capture) from a thread like the aasdk io_service, records the default sink's monitor and reports
// latency (write -> monitor), silence gaps and lost/inserted samples. Optional load on the Qt main thread.
//
//   audio_bench --stream media|guidance --seconds N --timing gaps.txt
//               [--ui-busy MS --ui-period MS] [--spike MS --spike-every MS] [--rate F]

#include <QCoreApplication>
#include <QTimer>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace f1x::openauto::autoapp::projection;
using Clock = std::chrono::steady_clock;

static double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

static void spin(int ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(ms);
  while (Clock::now() < end) {}
}

struct Options {
  std::string stream = "media", timing;
  int seconds = 60, uiBusy = 0, uiPeriod = 16, spike = 0, spikeEvery = 3000, hiccup = 0, hiccupEvery = 10, burst = 0;
  double rate = 1.0;
};

constexpr int cRecRate = 48000;
constexpr double cMarkerEvery = 0.5;  // seconds (rounded to whole packets)

struct Recording {
  std::mutex mutex;
  std::vector<int16_t> samples;
  std::vector<std::pair<size_t, double>> stamps;  // index of a chunk's last sample, its capture time
};

// parec records the default sink's monitor (pulseaudio-utils, also on openhu-os); its few ms of latency are the
// same in every run
static void record(Recording &rec, std::atomic<bool> &running) {
  int fds[2];
  if (pipe(fds) != 0) std::exit(1);
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execlp("parec", "parec", "--raw", "--format=s16le", "--rate=48000", "--channels=1", "--latency-msec=5",
           "-d", "@DEFAULT_MONITOR@", static_cast<char *>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  std::vector<int16_t> chunk(cRecRate / 200);
  while (running) {
    size_t got = 0;
    auto *bytes = reinterpret_cast<char *>(chunk.data());
    while (got < chunk.size() * 2) {
      const ssize_t n = read(fds[0], bytes + got, chunk.size() * 2 - got);
      if (n <= 0) break;
      got += size_t(n);
    }
    if (got < chunk.size() * 2) break;
    const double t = now();
    std::lock_guard<std::mutex> lock(rec.mutex);
    rec.samples.insert(rec.samples.end(), chunk.begin(), chunk.end());
    rec.stamps.emplace_back(rec.samples.size() - 1, t);
  }
  kill(pid, SIGTERM);
  waitpid(pid, nullptr, 0);
  close(fds[0]);
}

static double percentile(std::vector<double> v, double p) {
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))];
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  Options o;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string k = argv[i], v = argv[i + 1];
    if (k == "--stream") o.stream = v;
    else if (k == "--seconds") o.seconds = std::stoi(v);
    else if (k == "--timing") o.timing = v;
    else if (k == "--ui-busy") o.uiBusy = std::stoi(v);
    else if (k == "--ui-period") o.uiPeriod = std::stoi(v);
    else if (k == "--spike") o.spike = std::stoi(v);
    else if (k == "--spike-every") o.spikeEvery = std::stoi(v);
    else if (k == "--rate") o.rate = std::stod(v);
    else if (k == "--hiccup") o.hiccup = std::stoi(v);
    else if (k == "--burst") o.burst = std::stoi(v);
    else if (k == "--hiccup-every") o.hiccupEvery = std::stoi(v);
  }

  const bool media = o.stream == "media";
  const uint32_t channels = media ? 2 : 1, rate = media ? 48000 : 16000;
  const uint32_t packetFrames = media ? 2048 : 1024;
  const double packetSeconds = double(packetFrames) / rate;
  const int markerEvery = std::max(1, int(std::lround(cMarkerEvery / packetSeconds)));

  // packet gaps in ms from the capture; scaled to the nominal packet length on average (no artificial drift),
  // pauses of the stream left out
  std::vector<double> gaps;
  if (!o.timing.empty()) {
    std::ifstream in(o.timing);
    for (double g; in >> g;)
      if (g < 200) gaps.push_back(g / 1000);
  }
  if (gaps.empty()) gaps.push_back(packetSeconds);
  const double mean = std::accumulate(gaps.begin(), gaps.end(), 0.0) / gaps.size();
  for (auto &g : gaps) g *= packetSeconds / mean / o.rate;

  if (o.uiBusy > 0) {
    auto *t = new QTimer(&app);
    QObject::connect(t, &QTimer::timeout, [&] { spin(o.uiBusy); });
    t->start(o.uiPeriod);
  }
  if (o.spike > 0) {
    auto *t = new QTimer(&app);
    QObject::connect(t, &QTimer::timeout, [&] { spin(o.spike); });
    t->start(o.spikeEvery);
  }

  Recording rec;
  std::atomic<bool> recording{true};
  std::thread recorder(record, std::ref(rec), std::ref(recording));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::vector<double> markerWrites;
  std::thread feeder([&] {
    // created, opened and started from this thread like ServiceFactory/MediaAudioService on the io_service
    IAudioOutput::Pointer out;
    out = IAudioOutput::Pointer(new QtAudioOutput(channels, 16, rate),
                                [](IAudioOutput *p) { static_cast<QtAudioOutput *>(p)->deleteLater(); });
    if (!out->open()) {
      std::fprintf(stderr, "open failed\n");
      std::exit(1);
    }
    out->start();

    std::vector<int16_t> buf(packetFrames * channels);
    uint64_t frame = 0;
    const double start = now();
    double next = start;
    size_t packets = size_t(o.seconds / packetSeconds);
    for (size_t p = 0; p < packets; ++p) {
      for (uint32_t i = 0; i < packetFrames; ++i, ++frame) {
        double x = 0.2 * std::sin(2 * M_PI * 440.0 * double(frame) / rate);
        // marker: burst at the start of every markerEvery-th packet, its length (3, 5, ... 17 ms) is the marker
        // number modulo 8: matches markers to writes when packets are lost
        const size_t marker = p / markerEvery;
        if (p % markerEvery == 0 && i < rate * (3 + 2 * (marker % 8)) / 1000)
          x = 0.9 * std::sin(2 * M_PI * 2000.0 * i / rate);
        for (uint32_t c = 0; c < channels; ++c) buf[i * channels + c] = int16_t(x * 32767);
      }
      if (p % markerEvery == 0) markerWrites.push_back(now());
      out->write(0, aasdk::common::DataConstBuffer(buf.data(), buf.size() * 2));
      // the first o.burst packets at once, like the phone after a start (captures: 4-6 packets within ~20 ms)
      if (p + 1 >= size_t(o.burst)) next += gaps[p % gaps.size()];
      // transmission stall: nothing for o.hiccup ms, then the delayed packets at once (schedule unchanged)
      if (o.hiccup > 0 && p > 0 && p % size_t(o.hiccupEvery / packetSeconds) == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(o.hiccup));
      std::this_thread::sleep_until(Clock::time_point(std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(next))));
    }
    out->suspend();  // like the phone's stop indication: the rest plays out
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    out->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    out.reset();
    QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
  });

  app.exec();
  feeder.join();
  recording = false;
  recorder.join();

  // analysis
  const auto &s = rec.samples;
  auto timeOf = [&](size_t idx) {
    auto it = std::lower_bound(rec.stamps.begin(), rec.stamps.end(), std::make_pair(idx, -1e300));
    if (it == rec.stamps.end()) --it;
    return it->second - double(it->first - idx) / cRecRate;
  };
  std::vector<size_t> onsets, lengths;
  const int threshold = int(0.5 * 32767);
  for (size_t i = 0; i < s.size(); ++i) {
    if (std::abs(s[i]) <= threshold) continue;
    if (onsets.empty() || i - onsets.back() > cRecRate / 10) { onsets.push_back(i); lengths.push_back(0); }
    lengths.back() = i - onsets.back();
  }
  // marker number: the first onset is matched to the first write with its number modulo 8, every further one
  // to the next write with its number after the previous match
  std::vector<double> latencies;
  std::vector<std::pair<size_t, size_t>> matched;  // onset sample, marker number
  size_t next = 0, unmatched = 0;
  for (size_t k = 0; k < onsets.size(); ++k) {
    const double ms = lengths[k] * 1000.0 / cRecRate;
    const size_t mod = size_t(std::clamp(std::lround((ms - 3) / 2), 0L, 7L));
    while (next < markerWrites.size() && next % 8 != mod) ++next;
    if (next >= markerWrites.size()) { ++unmatched; continue; }
    latencies.push_back((timeOf(onsets[k]) - markerWrites[next]) * 1000);
    matched.emplace_back(onsets[k], next);
    ++next;
  }

  size_t gapCount = 0, gapSamples = 0, longest = 0;
  if (onsets.size() > 1) {
    size_t run = 0;
    for (size_t i = onsets.front(); i < onsets.back(); ++i) {
      if (std::abs(s[i]) < 64) ++run;
      else {
        if (run >= cRecRate / 1000) { ++gapCount; gapSamples += run; longest = std::max(longest, run); }
        run = 0;
      }
    }
  }
  // marker spacing against the nominal spacing: + inserted (silence), - lost samples
  const double nominal = markerEvery * packetSeconds * cRecRate;
  double inserted = 0, lost = 0;
  for (size_t k = 1; k < matched.size(); ++k) {
    const double d = double(matched[k].first - matched[k - 1].first) -
                     nominal * double(matched[k].second - matched[k - 1].second);
    if (d > 24) inserted += d;
    if (d < -24) lost -= d;
  }

  std::printf("stream=%s ui_busy=%d/%d spike=%d/%d hiccup=%d burst=%d rate=%.4f markers=%zu/%zu unmatched=%zu "
              "lat_min=%.1f lat_med=%.1f lat_p95=%.1f lat_max=%.1f lat_first=%.1f lat_last=%.1f "
              "gaps=%zu gap_ms=%.1f longest_gap_ms=%.1f inserted_ms=%.1f lost_ms=%.1f\n",
              o.stream.c_str(), o.uiBusy, o.uiPeriod, o.spike, o.spikeEvery, o.hiccup, o.burst, o.rate,
              onsets.size(), markerWrites.size(), unmatched, percentile(latencies, 0), percentile(latencies, 0.5),
              percentile(latencies, 0.95), percentile(latencies, 1),
              latencies.empty() ? NAN : latencies.front(), latencies.empty() ? NAN : latencies.back(),
              gapCount, gapSamples * 1000.0 / cRecRate, longest * 1000.0 / cRecRate, inserted * 1000 / cRecRate,
              lost * 1000 / cRecRate);
  return 0;
}
