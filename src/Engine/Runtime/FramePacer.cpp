#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/FramePacer.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace Runtime
{
    namespace
    {
        // Loop periods outside this range are not frames: they are startup, a scene load, a window
        // the compositor stopped feeding, or a clock that went backwards.
        constexpr double kMinSampleSeconds = 0.0001;   // 10000 fps
        constexpr double kMaxSampleSeconds = 0.5;      // 2 fps
        // Half a second to follow a real frame-rate change. The refresh snapping below removes the
        // ripple a burst would otherwise leave in an average this responsive.
        constexpr double kCadenceAlpha = 1.0 / 30.0;
        // The hold gate answers a slow question -- can this loop outrun the display at all -- so it
        // averages over a much longer window than the cadence estimate.
        constexpr double kUnheldAlpha = 1.0 / 120.0;
        // A frame this much longer than the interval is a hitch, not a burst. Hand the raw delta
        // through so the simulation does not replay the gap in slow motion.
        constexpr double kHitchIntervals = 6.0;
        // How far the simulation may run ahead of wall clock. One burst of a triple-buffered
        // swapchain costs three intervals, so four leaves headroom without letting the simulation
        // drift away if the cadence estimate is briefly wrong.
        constexpr double kMaxLeadIntervals = 4.0;
        // ...and how far it may fall behind before it starts catching up.
        constexpr double kMaxLagIntervals = 2.0;
        // Hold() ignores anything smaller than this much of a refresh period: ordinary jitter, not
        // a burst.
        constexpr double kHoldDeadband = 0.25;
        // Holding is only allowed while the loop, measured with the hold subtracted, cannot outrun
        // the display. A loop whose real mean period is well under one refresh period means the
        // refresh rate we were told is wrong, and holding against it would cap the frame rate.
        constexpr double kMinRefreshRatioForHold = 0.9;
        // Refresh snapping is only a way to remove ripple from the average, so it is applied only
        // when the average already agrees with a whole number of refresh periods. It has to be this
        // strict: SDL reports the mode of the display the window sits on, which is not always the
        // cadence the swapchain is presented at (a 165 Hz mode composited at 60, for one), and
        // snapping 16.7 ms onto three 6.06 ms refreshes would run the simulation 9% fast.
        constexpr double kSnapTolerance = 0.05;
        // Leave the last slice to a spin: a coarse sleep overshoots by a scheduler quantum, which
        // is most of a frame at 60 Hz.
        constexpr auto kSpinThreshold = std::chrono::microseconds(1200);

        double Ema(const double previous, const double sample, const double alpha)
        {
            return previous > 0.0 ? previous + (sample - previous) * alpha : sample;
        }
    }

    void FFramePacer::Configure(const FConfig& config)
    {
        const bool cadenceChanged = config.vsyncPresentMode != config_.vsyncPresentMode ||
            config.displayRefreshHz != config_.displayRefreshHz;
        config_ = config;
        if (cadenceChanged)
        {
            Reset();
        }
    }

    void FFramePacer::Reset()
    {
        cadenceEmaSeconds_ = 0.0;
        unheldEmaSeconds_ = 0.0;
        intervalSeconds_ = 0.0;
        pendingSeconds_ = 0.0;
        lastHoldSeconds_ = 0.0;
        nextSlotValid_ = false;
    }

    double FFramePacer::RefreshPeriodSeconds() const
    {
        return config_.displayRefreshHz > 0.0 ? 1.0 / config_.displayRefreshHz : 0.0;
    }

    bool FFramePacer::IsPacingDelta() const
    {
        return config_.paceDelta && config_.vsyncPresentMode && intervalSeconds_ > 0.0;
    }

    bool FFramePacer::IsLimitingBurst() const
    {
        const double refreshPeriod = RefreshPeriodSeconds();
        if (!config_.limitBurst || !config_.vsyncPresentMode || refreshPeriod <= 0.0 ||
            unheldEmaSeconds_ <= 0.0)
        {
            return false;
        }
        // Measured with the hold removed, so engaging the hold can never justify itself.
        return unheldEmaSeconds_ >= refreshPeriod * kMinRefreshRatioForHold;
    }

    void FFramePacer::Hold()
    {
        lastHoldSeconds_ = 0.0;
        if (!IsLimitingBurst())
        {
            nextSlotValid_ = false;
            return;
        }

        // Slots are spaced by the refresh period, never by the measured loop interval: under vsync
        // one image per refresh is the fastest the display can consume frames, so a hold at that
        // spacing costs a loop that keeps up nothing, and a loop that is slower never waits at all.
        // Pacing against the measured interval instead would let a slow startup convince the pacer
        // to keep the loop slow.
        const double refreshPeriod = RefreshPeriodSeconds();
        const auto period = std::chrono::duration_cast<FClock::duration>(
            std::chrono::duration<double>(refreshPeriod));
        const auto now = FClock::now();
        if (!nextSlotValid_)
        {
            nextSlot_ = now + period;
            nextSlotValid_ = true;
            return;
        }

        const auto deadband = std::chrono::duration_cast<FClock::duration>(
            std::chrono::duration<double>(refreshPeriod * kHoldDeadband));
        if (nextSlot_ > now + deadband)
        {
            // Never hold longer than one refresh period: one frame is then the whole cost of a
            // wrong estimate.
            const auto target = std::min(nextSlot_, now + period);
            if (target - now > kSpinThreshold)
            {
                std::this_thread::sleep_for(target - now - kSpinThreshold);
            }
            while (FClock::now() < target)
            {
                std::this_thread::yield();
            }
            lastHoldSeconds_ = std::chrono::duration<double>(FClock::now() - now).count();
        }

        const auto after = FClock::now();
        nextSlot_ += period;
        if (nextSlot_ < after)
        {
            // The loop is behind the cadence; re-anchor instead of accumulating a debt it would
            // then have to sprint off.
            nextSlot_ = after + period;
        }
    }

    double FFramePacer::Pace(const double rawDeltaSeconds)
    {
        // What this loop's period would have been without the hold. This, and not the raw period,
        // is what decides whether holding is allowed at all.
        const double unheldSeconds = std::max(0.0, rawDeltaSeconds - lastHoldSeconds_);
        if (unheldSeconds > kMinSampleSeconds && unheldSeconds < kMaxSampleSeconds)
        {
            unheldEmaSeconds_ = Ema(unheldEmaSeconds_, unheldSeconds, kUnheldAlpha);
        }

        if (rawDeltaSeconds > kMinSampleSeconds && rawDeltaSeconds < kMaxSampleSeconds)
        {
            cadenceEmaSeconds_ = Ema(cadenceEmaSeconds_, rawDeltaSeconds, kCadenceAlpha);
        }
        const double refreshPeriod = RefreshPeriodSeconds();
        if (cadenceEmaSeconds_ <= 0.0)
        {
            intervalSeconds_ = 0.0;
        }
        else
        {
            intervalSeconds_ = cadenceEmaSeconds_;
            if (refreshPeriod > 0.0)
            {
                // Snap to a whole number of refresh periods: a loop that keeps up sits on 1, one
                // that misses every other vblank on 2, and a burst leaves no ripple in the result
                // at all -- but only when the measurement already agrees, because the measurement
                // is the one that knows how often frames actually reach the screen.
                const double refreshes = std::clamp(
                    std::round(cadenceEmaSeconds_ / refreshPeriod), 1.0, 8.0);
                const double snapped = refreshPeriod * refreshes;
                if (std::abs(cadenceEmaSeconds_ - snapped) <= snapped * kSnapTolerance)
                {
                    intervalSeconds_ = snapped;
                }
            }
        }

        if (!IsPacingDelta())
        {
            pendingSeconds_ = 0.0;
            return rawDeltaSeconds;
        }

        if (rawDeltaSeconds < 0.0 || rawDeltaSeconds > intervalSeconds_ * kHitchIntervals)
        {
            pendingSeconds_ = 0.0;
            return std::max(0.0, rawDeltaSeconds);
        }

        pendingSeconds_ += rawDeltaSeconds;

        // One presented frame is one display period of visible motion, whatever the CPU loop did.
        double delta = intervalSeconds_;
        const double lead = delta - pendingSeconds_;
        if (lead > intervalSeconds_ * kMaxLeadIntervals)
        {
            delta = pendingSeconds_ + intervalSeconds_ * kMaxLeadIntervals;
        }
        else if (-lead > intervalSeconds_ * kMaxLagIntervals)
        {
            delta = pendingSeconds_ - intervalSeconds_ * kMaxLagIntervals;
        }

        delta = std::clamp(delta, 0.0, intervalSeconds_ * kHitchIntervals);
        pendingSeconds_ -= delta;
        return delta;
    }
}
