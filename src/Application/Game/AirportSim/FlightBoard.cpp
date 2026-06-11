#include "FlightBoard.h"

#include "AirportSimConfig.hpp"

#include <algorithm>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace AirportSim
{
    void FlightBoard::Reset(unsigned seed)
    {
        flights_.clear();
        events_.clear();
        rng_.seed(seed);
        generatedDay_ = -1;
    }

    void FlightBoard::GenerateDay(int dayIndex)
    {
        flights_.clear();
        std::uniform_int_distribution<int> countDist(Config::kMinFlightsPerDay, Config::kMaxFlightsPerDay);
        std::uniform_int_distribution<int> paxDist(Config::kMinPaxPerFlight, Config::kMaxPaxPerFlight);
        std::uniform_real_distribution<double> jitterDist(-20.0, 20.0);

        const int count = countDist(rng_);
        const double window = Config::kLastDeparture - Config::kFirstDeparture;
        double lastDepartPerGate[6] = {-1e9, -1e9, -1e9, -1e9, -1e9, -1e9};

        for (int i = 0; i < count; ++i)
        {
            // 07:00–21:00 均布 + 抖动防扎堆。
            double depart = Config::kFirstDeparture +
                            window * (static_cast<double>(i) + 0.5) / static_cast<double>(count) + jitterDist(rng_);
            depart = std::clamp(depart, Config::kFirstDeparture, Config::kLastDeparture);

            // 选同 gate 间隔 ≥90 分钟的登机口。
            int gate = static_cast<int>(rng_() % 6);
            for (int tries = 0; tries < 6; ++tries)
            {
                if (depart - lastDepartPerGate[gate] >= Config::kSameGateSpacingMinutes)
                {
                    break;
                }
                gate = (gate + 1) % 6;
            }
            lastDepartPerGate[gate] = depart;

            FFlight flight;
            flight.number = fmt::format("GK{}{:02d}", dayIndex % 9 + 1, i + 1);
            flight.gatePoi = fmt::format("gate_{:02d}", gate + 1);
            flight.departMinutes = depart;
            flight.paxTotal = paxDist(rng_);
            flight.colorIdx = i % 6;
            flights_.push_back(std::move(flight));
        }

        std::sort(flights_.begin(), flights_.end(),
                  [](const FFlight& a, const FFlight& b) { return a.departMinutes < b.departMinutes; });

        SPDLOG_INFO("AirportSim/Flights: day {} generated {} departures", dayIndex, flights_.size());
        for (const auto& f : flights_)
        {
            int hh = 0, mm = 0;
            MinutesToHHMM(f.departMinutes, hh, mm);
            SPDLOG_INFO("  {} {} dep {:02d}:{:02d} pax {}", f.number, f.gatePoi, hh, mm, f.paxTotal);
        }
    }

    void FlightBoard::Tick(int dayIndex, double dayMinutes)
    {
        if (generatedDay_ != dayIndex && dayMinutes >= Config::kDayStartMinutes)
        {
            generatedDay_ = dayIndex;
            GenerateDay(dayIndex);
        }

        for (size_t i = 0; i < flights_.size(); ++i)
        {
            FFlight& flight = flights_[i];
            EFlightState next = flight.state;
            const double toDepart = flight.departMinutes - dayMinutes;
            if (toDepart <= 0.0)
            {
                next = EFlightState::Departed;
            }
            else if (toDepart <= Config::kFinalCallLead)
            {
                next = EFlightState::Final;
            }
            else if (toDepart <= Config::kBoardingLead)
            {
                next = EFlightState::Boarding;
            }
            else if (toDepart <= Config::kCheckinOpenLead)
            {
                next = EFlightState::CheckinOpen;
            }

            if (next != flight.state && next > flight.state)
            {
                flight.state = next;
                events_.push_back({static_cast<int>(i), next});
                SPDLOG_INFO("AirportSim/Flights: {} -> {} (boarded {}/{})", flight.number, FlightStateName(next),
                            flight.paxBoarded, flight.paxTotal);
            }
        }
    }

    std::vector<FlightBoard::FFlightEvent> FlightBoard::ConsumeEvents()
    {
        std::vector<FFlightEvent> out;
        out.swap(events_);
        return out;
    }
}
