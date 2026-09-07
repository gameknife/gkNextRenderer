#pragma once

#include <chrono>

namespace Runtime
{
    // Frame pacing for a vsync-presented loop.
    //
    // Under a vsync present mode the display consumes exactly one presented image per refresh, so
    // the interval a frame is actually on screen for is the refresh period -- not the wall-clock
    // gap between two iterations of the CPU loop. Those two agree only while the presentation path
    // hands swapchain images back one per vblank. A layer that releases them in bursts (Streamline's
    // DLFG proxy swapchain is the case this was written for) makes the loop run three frames in
    // ~2 ms and then stall ~60 ms on the frame fence. The presented cadence stays a steady 60 Hz,
    // but a raw wall-clock delta feeds the simulation 0.7/0.7/0.7/60 ms while the display shows
    // those four frames 16.7 ms apart, and the picture judders hard.
    //
    // Two halves, both cheap and both no-ops on a healthy loop:
    //   Hold() stops the loop from draining a burst of already-acquired images faster than the
    //   display can show them, so each frame is rendered from input sampled when it will be shown.
    //   Pace() advances a simulation clock by one display period per presented frame and only lets
    //   it slide back towards wall clock when the loop is genuinely early or late.
    class FFramePacer final
    {
    public:
        struct FConfig
        {
            bool paceDelta = true;
            bool limitBurst = true;
            // Only vsync present modes are paced. An Immediate or Mailbox loop is meant to run as
            // fast as it can, and its wall-clock delta is already the right one.
            bool vsyncPresentMode = false;
            // 0 when unknown. Burst limiting needs a real refresh rate: holding frames against a
            // guessed period would cap the loop below what the display can actually show.
            double displayRefreshHz = 0.0;
        };

        void Configure(const FConfig& config);
        const FConfig& Config() const { return config_; }
        // Drops the accumulated pacing state. Call across anything that legitimately breaks the
        // cadence -- a scene load, a swapchain rebuild, a window that stopped rendering.
        void Reset();

        // Blocks until this frame's slot in the display cadence, when burst limiting is engaged.
        // Call at the top of the tick, before the frame's timestamp is taken.
        void Hold();
        // Wall-clock delta in, the delta the simulation should advance by out.
        double Pace(double rawDeltaSeconds);

        // Estimated interval between two presented frames. 0 until the estimate settles.
        double IntervalSeconds() const { return intervalSeconds_; }
        // Whether Pace() is currently replacing the wall-clock delta.
        bool IsPacingDelta() const;
        // Whether Hold() is currently allowed to block.
        bool IsLimitingBurst() const;

    private:
        using FClock = std::chrono::steady_clock;

        double RefreshPeriodSeconds() const;

        FConfig config_{};
        // Mean of the loop period. Under vsync the mean is the presented cadence whether the loop
        // is evenly paced or bursting, which is exactly the property this relies on.
        double cadenceEmaSeconds_ = 0.0;
        // Mean of the loop period with Hold()'s own wait subtracted. The hold gate reads this one
        // so that engaging the hold can never be what keeps the hold engaged.
        double unheldEmaSeconds_ = 0.0;
        // cadenceEmaSeconds_ snapped to a whole number of refresh periods when the refresh rate is
        // known, which removes the ripple a burst leaves in the average.
        double intervalSeconds_ = 0.0;
        // Wall-clock time the simulation has not consumed yet. Negative means the simulation is
        // ahead, which is the correct state in the middle of a burst.
        double pendingSeconds_ = 0.0;
        double lastHoldSeconds_ = 0.0;
        FClock::time_point nextSlot_{};
        bool nextSlotValid_ = false;
    };
}
