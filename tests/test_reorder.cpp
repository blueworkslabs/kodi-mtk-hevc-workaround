/*
 *  Tests for CMediaCodecOutputReorder (kodi-mtk-hevc-workaround).
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE in the repository root.
 */

// Replays decoder input/output sequences captured on a Fire TV Stick 4K Max (2nd gen) running
// Kodi 21.3 without the workaround, and checks what the guard hands over. The fixtures keep only
// decoder events; times are relative and every pts is shifted per playback epoch, so they do not
// reveal the title or position. The expected picture order around the two filmed bounces comes
// from matching slow-motion footage of the TV against a reference decode of the source, not from
// the guard. Lifecycle cases use small hand-written sequences with hand-written expectations.
//
// usage: test_reorder <fixture-dir> [--passthrough]
//   --passthrough hands every picture over as the decoder gave it (the behaviour before the fix)

#include "MediaCodecOutputReorder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Reorder = CMediaCodecOutputReorder;

namespace
{
int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

struct Handout
{
  int output; // index of the decoder output in the fixture
  int buffer;
  int64_t pts; // VideoPlayer pts, as the codec hands it over
  int64_t raw = 0; // decoder timebase
  int64_t label = 0; // decoder timebase, what the decoder reported for this output
};

struct Replay
{
  std::vector<std::vector<Handout>> epochs; // split at flush/reopen
  std::vector<std::set<int64_t>> inputs; // per epoch, decoder timebase
  std::vector<std::set<int64_t>> labels; // per epoch, decoder timebase, as reported by the decoder
  std::map<int, int> handedOrDiscarded; // output index -> times
  int outputs = 0;
  int holds = 0;
  int complete = 0;
  int gaveUp = 0;
  int expired = 0;
  int discarded = 0;
  std::vector<int64_t> restored; // VideoPlayer pts
};

// The codec's picture handling around the guard: one slot, a ready picture goes out on the next
// GetPicture() before anything else, flush discards the slot. Results are carried out by the same
// CMediaCodecOutputReorder::Apply() the codec uses.
struct Picture
{
  int output = -1;
  int buffer = -1;
  int64_t label = 0; // decoder timebase, what the decoder reported
  bool hasPts = true;
  int64_t pts = 0; // as handed over
};

struct FakeCodec
{
  Reorder reorder;
  Picture slot;
  bool slotFull = false;
  bool ready = false;
  std::vector<Picture>* handed = nullptr;
  int holds = 0, complete = 0, gaveUp = 0, expired = 0, discarded = 0;
  std::vector<int64_t> restored; // decoder timebase

  void Count(const Reorder::Result& r)
  {
    switch (r.event)
    {
      case Reorder::Event::HOLD: ++holds; break;
      case Reorder::Event::COMPLETE: ++complete; break;
      case Reorder::Event::GAVE_UP: ++gaveUp; break;
      case Reorder::Event::EXPIRED: ++expired; break;
      default: break;
    }
  }
  void Carry(const Reorder::Result& r, const Picture& current)
  {
    Count(r);
    Reorder::Apply(
        r, current.label,
        [&](Reorder::Source source, int64_t pts)
        {
          Picture p = source == Reorder::Source::CURRENT ? current : slot;
          if (source == Reorder::Source::HELD)
            slotFull = false;
          if (p.hasPts)
          {
            if (source == Reorder::Source::CURRENT && pts != p.label)
              restored.push_back(pts);
            p.pts = pts;
          }
          handed->push_back(p);
        },
        [&](Reorder::Source source, int64_t pts, bool isReady)
        {
          Picture p = source == Reorder::Source::CURRENT ? current : slot;
          if (p.hasPts)
          {
            if (source == Reorder::Source::CURRENT && pts != p.label)
              restored.push_back(pts);
            p.pts = pts;
          }
          slot = p;
          slotFull = true;
          ready = isReady;
        });
  }
  // start of every GetPicture(): a ready picture goes first, then the hold may expire
  void Begin(int64_t nowMs)
  {
    if (ready)
    {
      handed->push_back(slot);
      slotFull = ready = false;
    }
    Carry(reorder.Expire(nowMs), Picture{});
  }
  void Output(const Picture& p, int64_t nowMs)
  {
    Begin(nowMs);
    Carry(p.hasPts ? reorder.AddOutput(p.label, nowMs) : reorder.AddUnlabelledOutput(), p);
  }
  void Drain(int64_t nowMs)
  {
    Begin(nowMs);
    Carry(reorder.Abandon(), Picture{});
  }
  void EndOfStream(int64_t nowMs)
  {
    Begin(nowMs);
    Carry(reorder.Flush(), Picture{});
    Begin(nowMs);
  }
  void Flush()
  {
    reorder.Reset();
    if (slotFull)
      ++discarded;
    slotFull = ready = false;
  }
};

Replay Run(const std::string& path, bool passthrough)
{
  Replay replay;
  replay.epochs.emplace_back();
  replay.inputs.emplace_back();
  replay.labels.emplace_back();

  std::ifstream in(path);
  if (!in)
  {
    std::printf("cannot open %s\n", path.c_str());
    std::exit(2);
  }

  FakeCodec codec;
  std::vector<Picture> handed;
  codec.handed = &handed;
  double shift = 0;
  auto vp = [&](int64_t raw) { return static_cast<int64_t>(std::llround(raw + shift)); };
  // pictures handed over since the last call belong to the current epoch
  auto collect = [&]()
  {
    for (const Picture& p : handed)
    {
      replay.epochs.back().push_back({p.output, p.buffer, vp(p.pts), p.pts, p.label});
      replay.handedOrDiscarded[p.output]++;
    }
    handed.clear();
    for (const int64_t raw : codec.restored)
      replay.restored.push_back(vp(raw));
    codec.restored.clear();
  };

  std::string line;
  while (std::getline(in, line))
  {
    std::istringstream fields(line);
    int64_t ms = 0;
    std::string kind;
    fields >> ms >> kind;

    if (kind == "S")
    {
      fields >> shift;
    }
    else if (kind == "I")
    {
      int64_t pts = 0;
      fields >> pts;
      replay.inputs.back().insert(pts);
      if (!passthrough)
        codec.reorder.AddInput(pts);
    }
    else if (kind == "O")
    {
      Picture p;
      fields >> p.buffer >> p.label;
      p.output = replay.outputs++;
      p.pts = p.label;
      replay.labels.back().insert(p.label);
      if (passthrough)
        handed.push_back(p);
      else
        codec.Output(p, ms);
      collect();
    }
    else if (kind == "R" || kind == "X")
    {
      if (!passthrough)
      {
        const int before = codec.discarded;
        const Picture held = codec.slot;
        codec.Flush();
        if (codec.discarded != before)
          replay.handedOrDiscarded[held.output]++;
      }
      replay.epochs.emplace_back();
      replay.inputs.emplace_back();
      replay.labels.emplace_back();
      shift = 0;
    }
  }
  if (!passthrough)
    codec.EndOfStream(0);
  collect();

  replay.holds = codec.holds;
  replay.complete = codec.complete;
  replay.gaveUp = codec.gaveUp;
  replay.expired = codec.expired;
  replay.discarded = codec.discarded;
  return replay;
}

std::string Describe(const std::vector<Handout>& seq, size_t from, size_t count)
{
  std::ostringstream out;
  for (size_t i = from; i < seq.size() && i < from + count; ++i)
    out << " [buf " << seq[i].buffer << " pts " << seq[i].pts << "]";
  return out.str();
}

// The filmed TV order: the five handed-over pictures must be exactly these buffers with these pts,
// back to back.
void CheckFilmedOrder(const Replay& replay, const std::vector<Handout>& expected, const char* name)
{
  for (const auto& epoch : replay.epochs)
  {
    for (size_t i = 0; i < epoch.size(); ++i)
    {
      if (epoch[i].buffer != expected[0].buffer || epoch[i].pts != expected[0].pts)
        continue;
      bool same = i + expected.size() <= epoch.size();
      for (size_t k = 0; same && k < expected.size(); ++k)
        same = epoch[i + k].buffer == expected[k].buffer && epoch[i + k].pts == expected[k].pts;
      Check(same, std::string(name) + ": handed over" + Describe(epoch, i, expected.size()) +
                      ", filmed order" + Describe(expected, 0, expected.size()));
      return;
    }
  }
  Check(false, std::string(name) + ": first picture of the filmed event never handed over");
}

// Pictures leave in presentation order, each exactly once, and a pts is either the decoder's own
// label or one that was queued: nothing is invented.
void CheckInvariants(const Replay& replay, const char* name)
{
  int outOfOrder = 0;
  int invented = 0;
  for (size_t e = 0; e < replay.epochs.size(); ++e)
  {
    const auto& epoch = replay.epochs[e];
    for (size_t i = 0; i < epoch.size(); ++i)
    {
      if (i > 0 && epoch[i].raw <= epoch[i - 1].raw)
        ++outOfOrder;
      if (epoch[i].raw != epoch[i].label && !replay.inputs[e].count(epoch[i].raw))
        ++invented;
    }
  }
  Check(replay.outputs > 0, std::string(name) + ": fixture has decoder output");
  Check(outOfOrder == 0, std::string(name) + ": " + std::to_string(outOfOrder) +
                             " pictures handed over out of presentation order");
  Check(invented == 0,
        std::string(name) + ": " + std::to_string(invented) + " pictures carry pts never queued");

  int notOnce = 0;
  for (int o = 0; o < replay.outputs; ++o)
  {
    const auto it = replay.handedOrDiscarded.find(o);
    if (it == replay.handedOrDiscarded.end() || it->second != 1)
      ++notOnce;
  }
  Check(notOnce == 0, std::string(name) + ": " + std::to_string(notOnce) +
                          " decoder outputs not handed over or discarded exactly once");
}

// Only the substituted pictures change pts; everything else keeps the decoder's pts. A substituted
// label is more than 1 ms away from every queued pts (the invented steps are ~16 ms); labels a
// microsecond off are the decoder's rounding, not substitutions.
void CheckOnlySubstitutedChange(const Replay& replay, const char* name)
{
  size_t substituted = 0;
  for (size_t e = 0; e < replay.labels.size(); ++e)
    for (const int64_t label : replay.labels[e])
    {
      const auto near = replay.inputs[e].lower_bound(label - 1000);
      if (near == replay.inputs[e].end() || *near > label + 1000)
        ++substituted;
    }
  Check(substituted > 0, std::string(name) + ": fixture contains substituted pts");
  Check(replay.restored.size() == substituted,
        std::string(name) + ": restored " + std::to_string(replay.restored.size()) + " pts for " +
            std::to_string(substituted) + " substituted labels");
  Check(replay.gaveUp == 0 && replay.expired == 0,
        std::string(name) + ": every hold completed (gave up " + std::to_string(replay.gaveUp) +
            ", expired " + std::to_string(replay.expired) + ")");
}

void TestFilmedRun(const std::string& dir, bool passthrough)
{
  const Replay replay = Run(dir + "/filmed-run.txt", passthrough);
  // filmed event 1: A D B C E on screen; D is buffer 7 (labelled .449), B and C came as buffers
  // 6 and 0 with invented labels
  CheckFilmedOrder(replay,
                   {{0, 12, 112324000}, {0, 6, 112366000}, {0, 0, 112407000},
                    {0, 7, 112449000}, {0, 8, 112491000}},
                   "filmed event 1");
  // filmed event 2, same pattern; D is buffer 0 (labelled .440), B and C came as buffers 6 and 7
  CheckFilmedOrder(replay,
                   {{0, 9, 103315000}, {0, 6, 103357000}, {0, 7, 103398000},
                    {0, 0, 103440000}, {0, 8, 103482000}},
                   "filmed event 2");
  CheckInvariants(replay, "filmed run");
  CheckOnlySubstitutedChange(replay, "filmed run");
  std::printf("filmed run: %d outputs, %d holds, %d complete, %d gave up, %d expired, %d discarded\n",
              replay.outputs, replay.holds, replay.complete, replay.gaveUp, replay.expired,
              replay.discarded);
}

void TestTimingRun(const std::string& dir, bool passthrough)
{
  const Replay replay = Run(dir + "/timing-run.txt", passthrough);
  CheckInvariants(replay, "timing run");
  CheckOnlySubstitutedChange(replay, "timing run");
  // the six input pts missing from that run's decoder output, found by comparing the logged
  // input and output sets of the run
  const std::set<int64_t> missing = {20388000, 27354000, 27395000,
                                     37405000, 48375000, 48416000};
  const std::set<int64_t> restored(replay.restored.begin(), replay.restored.end());
  Check(restored == missing, "timing run: restored pts are exactly the six missing input pts");
  std::printf("timing run: %d outputs, %d holds, %d complete, %d gave up, %d expired\n",
              replay.outputs, replay.holds, replay.complete, replay.gaveUp, replay.expired);
}

// --- lifecycle, hand-written sequences ------------------------------------------------------
// pts in microseconds, 24 fps-like spacing as in the captures: A 324000, B 366000, C 407000,
// D 449000, E 491000 (keyframe). In decode order D is queued before C and B.

constexpr int64_t A = 324000, B = 366000, C = 407000, D = 449000, E = 491000;

void Queue(Reorder& r, std::initializer_list<int64_t> pts)
{
  for (const int64_t p : pts)
    r.AddInput(p);
}

// A is out, D comes out early.
Reorder::Result StartPattern(Reorder& r)
{
  Queue(r, {A, D, C, B, E});
  r.AddOutput(A, 0);
  return r.AddOutput(D, 10);
}

void TestPatternRestored()
{
  Reorder r;
  auto res = StartPattern(r);
  Check(res.holdCurrent && res.count == 0, "pattern: early picture is held");
  res = r.AddOutput(D + 16000, 20); // B with invented pts
  Check(res.count == 1 && res.emit[0].source == Reorder::Source::CURRENT && res.emit[0].pts == B,
        "pattern: first late picture gets the earliest missing pts");
  res = r.AddOutput(D + 32000, 30); // C with invented pts
  Check(res.count == 2 && res.emit[0].pts == C && res.emit[1].source == Reorder::Source::HELD &&
            res.emit[1].pts == D,
        "pattern: second late picture, then the held picture");
  res = r.AddOutput(E, 40);
  Check(res.count == 1 && res.emit[0].pts == E && !r.IsHolding(), "pattern: keyframe passes");
}

void TestCleanStreamPasses()
{
  Reorder r;
  Queue(r, {A, D, C, B, E});
  for (const int64_t p : {A, B, C, D, E})
  {
    const auto res = r.AddOutput(p, 0);
    Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == p,
          "clean: pts " + std::to_string(p) + " passes at once");
  }
}

void TestResetWhileHolding()
{
  Reorder r;
  StartPattern(r);
  Check(r.Reset(), "reset: reports the held picture for discarding");
  Check(!r.IsHolding(), "reset: nothing held afterwards");
  // pts of the old epoch carry no meaning after the flush
  const auto res = r.AddOutput(D + 16000, 20);
  Check(res.count == 1 && res.emit[0].pts == D + 16000,
        "reset: no pts from before the flush is reused");
}

void TestFlushAtEndOfStream()
{
  Reorder r;
  StartPattern(r);
  const auto res = r.Flush();
  Check(res.count == 1 && res.emit[0].source == Reorder::Source::HELD && res.emit[0].pts == D,
        "eos: held picture is handed over with its own pts");
  Check(!r.IsHolding(), "eos: nothing held afterwards");
}

// The codec flushes and disables at the start of a drain; until the next reset nothing is held or
// relabelled, then the guard works again.
void TestDrainStopsHoldingUntilReset()
{
  Reorder r;
  StartPattern(r);
  r.Flush();
  r.Disable();
  auto res = r.AddOutput(D + 16000, 20);
  Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == D + 16000,
        "drain: late picture passes unchanged");
  const int64_t gop = 1001000; // the same pattern one second later
  Queue(r, {E + gop, D + gop, C + gop, B + gop});
  r.AddOutput(A + gop, 30);
  res = r.AddOutput(D + gop, 40);
  Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == D + gop,
        "drain: no new hold until the next reset");
  r.Reset();
  res = StartPattern(r);
  Check(res.holdCurrent, "drain: after the next reset the pattern is recognised again");
}

void TestExpiry()
{
  Reorder r;
  Queue(r, {A, D, C, B, E});
  r.AddOutput(A, 1000);
  r.AddOutput(D, 1000);
  Check(r.Expire(1000 + Reorder::MAX_HOLD_MS).count == 0, "expiry: not before the limit");
  const auto res = r.Expire(1000 + Reorder::MAX_HOLD_MS + 1);
  Check(res.count == 1 && res.emit[0].pts == D, "expiry: held picture leaves after the limit");
}

void TestPatternDoesNotContinue()
{
  Reorder r;
  StartPattern(r);
  const auto res = r.AddOutput(E, 20); // the keyframe, the late pictures never come
  Check(res.count == 2 && res.emit[0].source == Reorder::Source::HELD && res.emit[0].pts == D &&
            res.emit[1].pts == E,
        "gave up: held picture first, then the keyframe, both with their own pts");
}

// An unknown pts far away is not a late picture (review reproducer).
void TestUnrelatedLabelGivesUp()
{
  Reorder r;
  StartPattern(r);
  const auto res = r.AddOutput(5000000, 20);
  Check(res.count == 2 && res.emit[0].source == Reorder::Source::HELD && res.emit[0].pts == D &&
            res.emit[1].source == Reorder::Source::CURRENT && res.emit[1].pts == 5000000,
        "unrelated unknown pts: held picture first, then the unknown pts unchanged");
}

void TestLargeStepGivesUp()
{
  Reorder r;
  StartPattern(r);
  const auto res = r.AddOutput(D + 30000, 20); // never queued, but 30 ms above the last label
  Check(res.count == 2 && res.emit[0].pts == D && res.emit[1].pts == D + 30000,
        "unknown pts too far above the last label: not taken as late");
}

void TestLabelPastNextRealPictureGivesUp()
{
  Reorder r;
  Queue(r, {A, D, C, B, D + 10000});
  r.AddOutput(A, 0);
  r.AddOutput(D, 10);
  const auto res = r.AddOutput(D + 16000, 20); // a real picture is due before this label
  Check(res.count == 2 && res.emit[0].pts == D && res.emit[1].pts == D + 16000,
        "unknown pts beyond the next queued picture: not taken as late");
}

void TestLongGapDoesNotHold()
{
  Reorder r;
  Queue(r, {A, A + 100000, A + 250000});
  r.AddOutput(A, 0);
  const auto res = r.AddOutput(A + 250000, 10); // 250 ms after the last picture
  Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == A + 250000,
        "picture far after the last one: not held");
}

void TestUnknownPtsWithNothingMissing()
{
  Reorder r;
  Queue(r, {A, B});
  r.AddOutput(A, 0);
  const auto res = r.AddOutput(A + 16000, 0);
  Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == A + 16000,
        "unknown pts with nothing missing passes unchanged");
}

void TestTooManyMissing()
{
  Reorder r;
  Queue(r, {A, A + 20000, A + 40000, A + 60000, A + 80000, A + 100000});
  r.AddOutput(A, 0);
  const auto res = r.AddOutput(A + 100000, 0); // four pictures would be missing
  Check(!res.holdCurrent && res.count == 1, "more than MAX_MISSING missing: no hold");
}

void TestPicturesBeforeLastOutputDoNotHold()
{
  Reorder r;
  Queue(r, {A, B, C, D}); // B is lost by the decoder
  r.AddOutput(A, 0);
  r.AddOutput(C, 0); // B still missing: held until D shows the pattern does not continue
  r.AddOutput(D, 0);
  Queue(r, {E});
  const auto res = r.AddOutput(E, 0);
  Check(!res.holdCurrent && res.count == 1 && res.emit[0].pts == E,
        "a lost picture older than the last handed-out one never makes later pictures wait");
}

void TestDuplicateInputDisables()
{
  Reorder r;
  Queue(r, {A, D, C, B, E, E});
  r.AddOutput(A, 0);
  const auto res = r.AddOutput(D, 0);
  Check(!res.holdCurrent && res.count == 1, "duplicate queued pts: everything passes unchanged");
}

void TestDisableWhileHolding()
{
  Reorder r;
  StartPattern(r);
  r.Disable();
  const auto res = r.AddOutput(D + 16000, 20);
  Check(res.count == 2 && res.emit[0].source == Reorder::Source::HELD &&
            res.emit[1].pts == D + 16000,
        "disabled while holding: held picture first, then unchanged");
}

// --- the codec's slot sequencing (FakeCodec carries results out with the codec's Apply()) -----

struct Sequence
{
  FakeCodec codec;
  std::vector<Picture> handed;
  int next = 0;
  Sequence() { codec.handed = &handed; }
  void Out(int64_t label, int64_t ms = 0) { codec.Output({next++, -1, label, true, label}, ms); }
  void OutWithoutPts(int64_t ms = 0) { codec.Output({next++, -1, 0, false, 0}, ms); }
  std::string Order() const
  {
    std::string order;
    for (const Picture& p : handed)
      order += std::to_string(p.output) + (p.hasPts ? ":" + std::to_string(p.pts) : ":none") + " ";
    return order;
  }
};

void TestSlotPatternOrder()
{
  Sequence q;
  Queue(q.codec.reorder, {A, D, C, B, E});
  q.Out(A); q.Out(D); q.Out(D + 16000); q.Out(D + 32000); q.Out(E);
  q.codec.EndOfStream(0);
  const std::string want = "0:" + std::to_string(A) + " 2:" + std::to_string(B) + " 3:" +
                           std::to_string(C) + " 1:" + std::to_string(D) + " 4:" +
                           std::to_string(E) + " ";
  Check(q.Order() == want, "slots: pattern handed over as A B C D E, got " + q.Order());
}

// A picture without pts while D is held: decoder order A D X E, nothing reordered afterwards.
void TestSlotPictureWithoutPts()
{
  Sequence q;
  Queue(q.codec.reorder, {A, D, C, B, E});
  q.Out(A); q.Out(D); q.OutWithoutPts(); q.Out(E);
  const std::string want = "0:" + std::to_string(A) + " 1:" + std::to_string(D) + " 2:none 3:" +
                           std::to_string(E) + " ";
  Check(q.Order() == want, "slots: picture without pts keeps decoder order A D X E, got " +
                               q.Order());
  const int64_t gop = 1001000;
  Queue(q.codec.reorder, {A + gop, D + gop, C + gop, B + gop});
  q.Out(A + gop); q.Out(D + gop); q.Out(D + gop + 16000);
  Check(q.codec.holds == 1 && q.handed.size() == 7 && q.handed[6].pts == D + gop + 16000,
        "slots: nothing reordered after a picture without pts until the next flush");
}

// A label within rounding of D is not a late picture.
void TestSlotNearRepeatOfHeldPicture()
{
  for (const int64_t repeat : {D + 1, D + 2, D + Reorder::MIN_STEP_US - 1})
  {
    Sequence q;
    Queue(q.codec.reorder, {A, D, C, B, E});
    q.Out(A); q.Out(D); q.Out(repeat);
    q.codec.EndOfStream(0);
    const std::string want = "0:" + std::to_string(A) + " 1:" + std::to_string(D) + " 2:" +
                             std::to_string(repeat) + " ";
    Check(q.Order() == want, "slots: label " + std::to_string(repeat - D) +
                                 " us above D passes unchanged after D, got " + q.Order());
  }
}

void TestSlotDrain()
{
  Sequence q;
  Queue(q.codec.reorder, {A, D, C, B, E});
  q.Out(A); q.Out(D);
  q.codec.Drain(0);
  q.Out(D + 16000);
  const std::string want = "0:" + std::to_string(A) + " 1:" + std::to_string(D) + " 2:" +
                           std::to_string(D + 16000) + " ";
  Check(q.Order() == want, "slots: drain hands D over at once, later labels unchanged, got " +
                               q.Order());
}

void TestSlotFlushDiscards()
{
  Sequence q;
  Queue(q.codec.reorder, {A, D, C, B, E});
  q.Out(A); q.Out(D);
  q.codec.Flush();
  q.codec.EndOfStream(0);
  Check(q.handed.size() == 1 && q.codec.discarded == 1,
        "slots: flush discards the held picture, it is never handed over");
}

void TestSlotEndOfStream()
{
  Sequence q;
  Queue(q.codec.reorder, {A, D, C, B, E});
  q.Out(A); q.Out(D);
  q.codec.EndOfStream(0);
  Check(q.handed.size() == 2 && q.handed[1].output == 1 && q.handed[1].pts == D,
        "slots: end of stream hands the held picture over");
}

void TestDroppedLatePicture()
{
  Reorder r;
  StartPattern(r);
  auto res = r.DropOutput(D + 16000); // B dropped by the player
  Check(res.count == 0 && r.IsHolding(), "drop: one late picture dropped, still waiting for C");
  res = r.AddOutput(D + 32000, 30);
  Check(res.count == 2 && res.emit[0].pts == C && res.emit[1].pts == D,
        "drop: C gets its own pts, then the held picture");
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::printf("usage: %s <fixture-dir> [--passthrough]\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];
  const bool passthrough = argc > 2 && std::string(argv[2]) == "--passthrough";

  TestFilmedRun(dir, passthrough);
  TestTimingRun(dir, passthrough);
  if (!passthrough)
  {
    TestPatternRestored();
    TestCleanStreamPasses();
    TestResetWhileHolding();
    TestFlushAtEndOfStream();
    TestDrainStopsHoldingUntilReset();
    TestExpiry();
    TestPatternDoesNotContinue();
    TestUnrelatedLabelGivesUp();
    TestLargeStepGivesUp();
    TestLabelPastNextRealPictureGivesUp();
    TestLongGapDoesNotHold();
    TestUnknownPtsWithNothingMissing();
    TestTooManyMissing();
    TestPicturesBeforeLastOutputDoNotHold();
    TestDuplicateInputDisables();
    TestDisableWhileHolding();
    TestDroppedLatePicture();
    TestSlotPatternOrder();
    TestSlotPictureWithoutPts();
    TestSlotNearRepeatOfHeldPicture();
    TestSlotDrain();
    TestSlotFlushDiscards();
    TestSlotEndOfStream();
  }

  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
