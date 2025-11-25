// sim_server.cpp — Multi-elevator passenger simulation with real stats.
// Windows build (MinGW):
//   g++ sim_server.cpp -o sim_server -std=c++17 -lws2_32
// Run:
//   .\sim_server
//
// Endpoints:
//   GET /state
//   GET /stats/daily
//
// IMPORTANT FLOOR FIX:
// Internally the sim logic is unchanged,
// BUT floors are served as: 1 = bottom, gFloors = top.
// We convert on output:
//   publicFloor = gFloors - internalFloor + 1
//   publicDirection = -internalDirection
//
// OPTION B PATCH (THIS FILE):
// - Simulation logic unchanged except for dispatching.
// - Energy model replaced with MATLAB-accurate physics.
// - Adds stop-queue + least-cost dispatcher.
// - Dedupes stops AND floor calls (Up/Down independently).
// - Adds state + remainingMs back to /state for Android smooth animation.

#include <iostream>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>
#include <string>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct Passenger {
    int startFloor;
    int destFloor;
    int direction;     // +1 up, -1 down (internal)
    TimePoint created;
};

enum class ElevatorState { Idle, Moving, DoorOpen };

struct Elevator {
    int id;
    int currentFloor;  // internal numbering
    int targetFloor;   // internal numbering
    int direction;     // internal: +1 up, -1 down, 0 idle
    bool doorOpen;
    ElevatorState state;
    TimePoint stateEndTime;

    int capacity = 10;
    std::vector<Passenger> onboard;

    // NEW: Planned future stops (internal floors)
    std::deque<int> stops;

    // per-elevator stats (UNCHANGED FIELDS)
    int trips = 0;
    int passengersMoved = 0;
    double energyKWh = 0.0;       // NET energy (consumed - regenerated) in kWh
    int doorOpenCount = 0;
    int stopCount = 0;
};

struct HourlyBucket {
    int trips = 0;
    double energyKWh = 0.0;       // NET energy per hour (kWh)
    double totalWaitSec = 0.0;
    int waitCount = 0;
};

struct GlobalStats {
    int totalTrips = 0;
    int totalPassengers = 0;
    int completedPassengers = 0;

    double totalEnergyKWh = 0.0;  // NET kWh
    double totalWaitSec = 0.0;
    double totalTripSec = 0.0;
    int completedTrips = 0;

    // ---------- NEW OPTION 2 ECONOMICS ----------
    double totalEnergyConsumedWh = 0.0;     // MATLAB energyConsumed
    double totalEnergyRegenWh = 0.0;        // MATLAB energyRegenerated
    double totalNetEnergyWh = 0.0;          // consumed - regen

    double totalCostCAD = 0.0;              // net energy * TOU
    double costTraditionalCAD = 0.0;        // consumed * TOU (no regen)
};

int gFloors = 5;
std::vector<Elevator> gElevators;
std::vector<std::deque<Passenger>> upQ, downQ;
GlobalStats gStats;
HourlyBucket gHourly[24];
std::mutex gMutex;

// NEW: floor call latches (dedupe Up and Down separately)
std::vector<bool> pendingUpCall;
std::vector<bool> pendingDownCall;

std::mt19937& rng() {
    static std::mt19937 gen{ std::random_device{}() };
    return gen;
}

// timing: 1 floor 7.5s, middle floors 7s, last leg 7.5s
double travel_time_sec(int floors) {
    if (floors <= 1) return 7.5;
    return 7.5 + 7.5 + 7.0 * (floors - 2);
}

// fake hour (30 seconds real = 1 hour sim)
int fake_hour() {
    auto now = Clock::now().time_since_epoch();
    long sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return (sec / 30) % 24;
}

double spawn_rate_per_min(int h) {
    if (h >= 7 && h < 10) return 0.25;
    if (h >= 11 && h < 14) return 0.15;
    if (h >= 16 && h < 19) return 0.30;
    return 0.05;
}

bool should_spawn(double ratePerSec) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng()) < ratePerSec;
}

// -------- FLOOR OUTPUT CONVERSION HELPERS --------
// Convert internal floor to public floor shown to app.
// Public: 1 = bottom, gFloors = top.
int toPublicFloor(int internalFloor) {
    return gFloors - internalFloor + 1;
}

// Convert internal direction to public direction.
// If we flip floors, direction sign flips too.
int toPublicDirection(int internalDir) {
    return -internalDir;
}

// ---------------- MATLAB CONSTANTS ----------------
struct MatlabParams {
    double floorHeight_m = 5.0;
    double elevatorSpeed_mps = 1.5;

    double elevatorCarMass_kg = 500.0;
    double counterWeight_kg   = 1400.0;

    double motorEfficiency    = 0.85;
    double regenEfficiency    = 0.78;
    double supercapEfficiency = 0.95;

    double personMass_kg      = 65.0;
} P;

double ontario_rate_cad_per_kwh(int hour) {
    if (hour >= 23 || hour < 7) return 0.028;
    if (hour >= 7 && hour < 16) return 0.122;
    if (hour >= 21 && hour < 23) return 0.122;
    return 0.284; // 16-21
}

struct EnergyResult {
    double consumedWh;
    double regenWh;
    double netWh;
    double netMassKg;
};

EnergyResult calculateElevatorEnergyMATLAB(
    int startFloorInternal,
    int endFloorInternal,
    int passengerCount
) {
    double loadKg = passengerCount * P.personMass_kg;
    double netMass = loadKg + P.elevatorCarMass_kg - P.counterWeight_kg;

    double distance = std::abs(endFloorInternal - startFloorInternal) * P.floorHeight_m;

    double energyConsumed = 0.0;
    double energyRegen = 0.0;

    if (endFloorInternal > startFloorInternal) {
        // Ascending
        if (netMass > 0) {
            double potentialJ = netMass * 9.8 * distance;
            double potentialWh = potentialJ / 3600.0;
            energyConsumed = potentialWh / P.motorEfficiency;
            energyRegen = 0.0;
        } else {
            energyConsumed = distance * 0.1;
            energyRegen = 0.0;
        }
    } else {
        // Descending
        if (netMass > 0) {
            double potentialJ = netMass * 9.8 * distance;
            double potentialWh = potentialJ / 3600.0;

            energyConsumed = potentialWh * 0.15;

            if (netMass > 400) {
                energyRegen = potentialWh * P.regenEfficiency * P.supercapEfficiency;
            } else {
                energyRegen = potentialWh * 0.5 * P.regenEfficiency * P.supercapEfficiency;
            }
        } else {
            double potentialJ = std::abs(netMass) * 9.8 * distance;
            double potentialWh = potentialJ / 3600.0;

            energyConsumed = potentialWh / P.motorEfficiency;
            energyRegen = 0.0;
        }
    }

    EnergyResult r;
    r.consumedWh = energyConsumed;
    r.regenWh = energyRegen;
    r.netWh = energyConsumed - energyRegen;
    r.netMassKg = netMass;
    return r;
}

Passenger make_passenger(int floor) {
    std::uniform_int_distribution<int> dist(1, gFloors);
    int dest = floor;
    while (dest == floor) dest = dist(rng());

    Passenger p;
    p.startFloor = floor;
    p.destFloor = dest;
    p.direction = (dest > floor ? +1 : -1);   // internal direction
    p.created = Clock::now();
    return p;
}

// Generate traffic and latch floor calls (Up/Down separately)
void generate_traffic() {
    int h = fake_hour();
    double rateMin = spawn_rate_per_min(h);
    double rateSec = rateMin / 60.0;

    for (int f = 1; f <= gFloors; ++f) {
        if (should_spawn(rateSec)) {
            Passenger p = make_passenger(f);

            if (p.direction == +1) {
                upQ[f].push_back(p);
                pendingUpCall[f] = true;     // latch up call
            } else {
                downQ[f].push_back(p);
                pendingDownCall[f] = true;   // latch down call
            }

            gStats.totalPassengers++;
        }
    }
}

// ---------------------------------------------------------
// LOW-LEVEL fallback target selection (unchanged)
// Used only if dispatcher hasn't assigned stops.
// ---------------------------------------------------------
int choose_next_target_fallback(const Elevator& e) {
    if (!e.onboard.empty())
        return e.onboard.front().destFloor;

    int best = e.currentFloor;
    int bestDist = 999;

    for (int f = 1; f <= gFloors; ++f) {
        if (upQ[f].empty() && downQ[f].empty()) continue;
        int d = std::abs(f - e.currentFloor);
        if (d < bestDist) {
            bestDist = d;
            best = f;
        }
    }
    return best;
}

// ---------------------------------------------------------
// YOUR LEAST COST SCORING + HYBRID ASSIGNMENT
// ---------------------------------------------------------
double leastCostScore(const Elevator &el, int callFloor, int callDir) {
    double timePerFloor = travel_time_sec(1);  // seconds per floor equivalent
    double pickupFloors = std::abs(el.currentFloor - callFloor);
    double pickupTime = pickupFloors * timePerFloor;

    bool wrongDir =
        (el.direction == 1 && callDir == -1) ||
        (el.direction == -1 && callDir == 1);

    double reversalPenalty = wrongDir ? 14.0 : 0.0;

    double queuePenalty = (double)el.stops.size() * 18.0;  // strong queue penalty
    double stopPenalty  = (!el.stops.empty()) ? 6.0 : 0.0;

    const double ALPHA = 1.8;
    const double BETA  = 1.3;
    const double GAMMA = 1.4;
    const double DELTA = 0.8;

    return ALPHA*pickupTime +
           BETA *reversalPenalty +
           GAMMA*queuePenalty +
           DELTA*stopPenalty;
}

// Two-stage Hybrid: top-K nearest → pick best cost
// Returns index into gElevators
int assignLeastCostHybrid(const std::vector<Elevator> &elevs, int callFloor, int callDir) {
    std::vector<std::pair<int,int>> byDist; // {dist, idx}
    byDist.reserve(elevs.size());

    for (int i = 0; i < (int)elevs.size(); ++i) {
        byDist.push_back({std::abs(elevs[i].currentFloor - callFloor), i});
    }

    std::sort(byDist.begin(), byDist.end());

    int K = std::min(2, (int)byDist.size()); // top-2 nearest
    int bestIdx = -1;
    double bestCost = 1e18;

    for (int i = 0; i < K; i++) {
        int idx = byDist[i].second;
        double c = leastCostScore(elevs[idx], callFloor, callDir);

        if (byDist[i].first == byDist[0].first) c -= 1.0; // tiny nearest bonus

        if (c < bestCost) {
            bestCost = c;
            bestIdx = idx;
        }
    }

    return bestIdx;
}

// ---------------------------------------------------------
// DISPATCHER: assigns floor calls into elevator stop queues
// Dedup rules:
//  - Up and Down calls are distinct; one of each max per floor.
//  - Once latched, call stays until queue empties at that floor.
//  - Stop queue never stores duplicates.
// ---------------------------------------------------------
void dispatch_calls() {
    // Build call list from latched floors
    for (int f = 1; f <= gFloors; ++f) {

        if (pendingUpCall[f]) {
            int best = assignLeastCostHybrid(gElevators, f, +1);
            if (best >= 0) {
                auto &el = gElevators[best];

                // Dedup pickup stop
                if (std::find(el.stops.begin(), el.stops.end(), f) == el.stops.end())
                    el.stops.push_back(f);
            }
        }

        if (pendingDownCall[f]) {
            int best = assignLeastCostHybrid(gElevators, f, -1);
            if (best >= 0) {
                auto &el = gElevators[best];

                if (std::find(el.stops.begin(), el.stops.end(), f) == el.stops.end())
                    el.stops.push_back(f);
            }
        }
    }
}

// ---------------------------------------------------------
// UPDATE ELEVATOR STATE MACHINE (logic unchanged except dispatch/stops)
// ---------------------------------------------------------
void update_elevator(Elevator& e, TimePoint now) {
    using namespace std::chrono;

    // -------- IDLE --------
    if (e.state == ElevatorState::Idle) {
        if (now >= e.stateEndTime) {

            int next = e.currentFloor;

            // If we have planned stops, serve them FIFO
            if (!e.stops.empty()) {
                next = e.stops.front();
                // If we're already there, pop and re-evaluate next tick
                if (next == e.currentFloor) {
                    e.stops.pop_front();
                    e.direction = 0;
                    e.stateEndTime = now + seconds(1);
                    return;
                }
            } else {
                // fallback if no stops assigned yet
                next = choose_next_target_fallback(e);
                if (next == e.currentFloor) {
                    e.direction = 0;
                    e.stateEndTime = now + seconds(1);
                    return;
                }
            }

            e.targetFloor = next;
            int diff = e.targetFloor - e.currentFloor;
            int floors = std::abs(diff);

            e.direction = diff > 0 ? +1 : -1;  // internal direction
            e.doorOpen = false;
            e.state = ElevatorState::Moving;

            double tSec = travel_time_sec(floors);
            e.stateEndTime = now + duration_cast<Clock::duration>(duration<double>(tSec));

            // Trip stats
            gStats.totalTrips++;
            gStats.completedTrips++;
            gStats.totalTripSec += tSec;
            e.trips++;

            int h = fake_hour();
            gHourly[h].trips++;
        }
    }

    // -------- MOVING --------
    else if (e.state == ElevatorState::Moving) {
        if (now >= e.stateEndTime) {

            // ------------------ ENERGY + COST PATCH (MATLAB) ------------------
            int startFloor = e.currentFloor;
            int endFloor   = e.targetFloor;
            int paxCount   = (int)e.onboard.size();

            EnergyResult er = calculateElevatorEnergyMATLAB(startFloor, endFloor, paxCount);

            int hNow = fake_hour();
            double rate = ontario_rate_cad_per_kwh(hNow);

            double netCost = er.netWh * rate / 1000.0;
            double traditionalCost = er.consumedWh * rate / 1000.0;

            gStats.totalEnergyConsumedWh += er.consumedWh;
            gStats.totalEnergyRegenWh    += er.regenWh;
            gStats.totalNetEnergyWh      += er.netWh;

            gStats.totalCostCAD          += netCost;
            gStats.costTraditionalCAD    += traditionalCost;

            double netKWh = er.netWh / 1000.0;
            gStats.totalEnergyKWh += netKWh;
            e.energyKWh += netKWh;
            gHourly[hNow].energyKWh += netKWh;
            // ---------------------------------------------------------------

            // Arrive at floor
            e.currentFloor = e.targetFloor;
            e.direction = 0;
            e.doorOpen = true;
            e.state = ElevatorState::DoorOpen;
            e.stateEndTime = now + seconds(5);  // door timing

            e.stopCount++;
            e.doorOpenCount++;

            // Remove this floor from planned stops (pickup or dropoff)
            e.stops.erase(
                std::remove(e.stops.begin(), e.stops.end(), e.currentFloor),
                e.stops.end()
            );

            // OFFLOAD passengers
            auto it = e.onboard.begin();
            while (it != e.onboard.end()) {
                if (it->destFloor == e.currentFloor) {
                    gStats.completedPassengers++;
                    e.passengersMoved++;
                    it = e.onboard.erase(it);
                } else {
                    ++it;
                }
            }

            // LOAD queues
            int capLeft = e.capacity - (int)e.onboard.size();
            auto& U = upQ[e.currentFloor];
            auto& D = downQ[e.currentFloor];

            auto board = [&](std::deque<Passenger>& q, int dirForLatch) {
                while (capLeft > 0 && !q.empty()) {
                    Passenger p = q.front();
                    q.pop_front();
                    double waitSec =
                        duration<double>(now - p.created).count();
                    gStats.totalWaitSec += waitSec;

                    int h2 = fake_hour();
                    gHourly[h2].totalWaitSec += waitSec;
                    gHourly[h2].waitCount++;

                    e.onboard.push_back(p);
                    capLeft--;

                    // NEW: add destination to stops, deduped
                    if (std::find(e.stops.begin(), e.stops.end(), p.destFloor) == e.stops.end())
                        e.stops.push_back(p.destFloor);
                }

                // If queue empty, clear that direction's pending call latch
                if (q.empty()) {
                    if (dirForLatch == +1) pendingUpCall[e.currentFloor] = false;
                    if (dirForLatch == -1) pendingDownCall[e.currentFloor] = false;
                }
            };

            board(U, +1);
            board(D, -1);
        }
    }

    // -------- DOOR OPEN --------
    else if (e.state == ElevatorState::DoorOpen) {
        if (now >= e.stateEndTime) {
            e.doorOpen = false;
            e.state = ElevatorState::Idle;
            e.stateEndTime = now + seconds(1);
        }
    }
}

void sim_loop() {
    while (true) {
        auto now = Clock::now();
        {
            std::lock_guard<std::mutex> lock(gMutex);
            generate_traffic();

            // NEW: dispatch floor calls into stop queues
            dispatch_calls();

            for (auto& e : gElevators)
                update_elevator(e, now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ------------------------------------------------------------------
// FIXED /state JSON, with state + remainingMs restored
// ------------------------------------------------------------------
std::string state_json() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::ostringstream out;

    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"elevators\":[";

    for (size_t i = 0; i < gElevators.size(); ++i) {
        const auto& e = gElevators[i];
        if (i) out << ",";

        int publicFloor     = toPublicFloor(e.currentFloor);
        int publicTarget    = toPublicFloor(e.targetFloor);
        int publicDirection = toPublicDirection(e.direction);

        std::string humanState;
        if (e.state == ElevatorState::Idle) humanState = "Idle";
        else if (e.state == ElevatorState::Moving) humanState = "Moving";
        else humanState = "DoorOpen";

        auto now = Clock::now();
        long remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               e.stateEndTime - now
                           ).count();
        if (remainingMs < 0) remainingMs = 0;

        out << "{"
            << "\"id\":" << e.id
            << ",\"currentFloor\":" << publicFloor
            << ",\"targetFloor\":" << publicTarget
            << ",\"direction\":" << publicDirection
            << ",\"doorOpen\":" << (e.doorOpen ? "true" : "false")
            << ",\"load\":" << e.onboard.size()
            << ",\"capacity\":" << e.capacity
            << ",\"state\":\"" << humanState << "\""
            << ",\"remainingMs\":" << remainingMs
            << "}";
    }

    out << "]}";
    return out.str();
}

// ------------------------------------------------------------------
// /stats/daily JSON (Option 2 additions unchanged)
// ------------------------------------------------------------------
std::string stats_json() {
    std::lock_guard<std::mutex> lock(gMutex);

    double avgWait =
        gStats.completedPassengers > 0
            ? gStats.totalWaitSec / gStats.completedPassengers
            : 0.0;

    double avgTrip =
        gStats.completedTrips > 0
            ? gStats.totalTripSec / gStats.completedTrips
            : 0.0;

    double avgEnergy =
        gStats.totalTrips > 0
            ? gStats.totalEnergyKWh / gStats.totalTrips
            : 0.0;

    int peakHour = 0, maxTrips = 0;
    for (int h = 0; h < 24; ++h)
        if (gHourly[h].trips > maxTrips) {
            maxTrips = gHourly[h].trips;
            peakHour = h;
        }

    double dailySavingsCAD = gStats.costTraditionalCAD - gStats.totalCostCAD;
    double regenPercent =
        (gStats.totalEnergyConsumedWh > 0.0)
            ? (gStats.totalEnergyRegenWh / gStats.totalEnergyConsumedWh) * 100.0
            : 0.0;

    std::ostringstream out;
    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"totalTrips\":" << gStats.totalTrips << ",";
    out << "\"totalPassengers\":" << gStats.totalPassengers << ",";
    out << "\"avgWaitSec\":" << avgWait << ",";
    out << "\"avgTripSec\":" << avgTrip << ",";
    out << "\"avgEnergyKWh\":" << avgEnergy << ",";
    out << "\"peakHour\":" << peakHour << ",";

    out << "\"totalEnergyConsumedWh\":" << gStats.totalEnergyConsumedWh << ",";
    out << "\"totalEnergyRegeneratedWh\":" << gStats.totalEnergyRegenWh << ",";
    out << "\"totalNetEnergyWh\":" << gStats.totalNetEnergyWh << ",";
    out << "\"totalCostCAD\":" << gStats.totalCostCAD << ",";
    out << "\"costTraditionalCAD\":" << gStats.costTraditionalCAD << ",";
    out << "\"dailySavingsCAD\":" << dailySavingsCAD << ",";
    out << "\"regenPercent\":" << regenPercent << ",";

    out << "\"elevators\":[";
    for (size_t i = 0; i < gElevators.size(); ++i) {
        const auto& e = gElevators[i];
        if (i) out << ",";
        out << "{"
            << "\"id\":" << e.id
            << ",\"trips\":" << e.trips
            << ",\"passengersMoved\":" << e.passengersMoved
            << ",\"energyKWh\":" << e.energyKWh
            << ",\"doorOpenCount\":" << e.doorOpenCount
            << ",\"stopCount\":" << e.stopCount
            << "}";
    }
    out << "],";

    out << "\"hourly\":[";
    for (int h = 0; h < 24; ++h) {
        if (h) out << ",";
        double hAvgWait =
            gHourly[h].waitCount > 0
                ? gHourly[h].totalWaitSec / gHourly[h].waitCount
                : 0.0;

        out << "{"
            << "\"hour\":" << h
            << ",\"trips\":" << gHourly[h].trips
            << ",\"avgWaitSec\":" << hAvgWait
            << ",\"energyKWh\":" << gHourly[h].energyKWh
            << "}";
    }
    out << "]}";

    return out.str();
}

// ------------------------------------------------------------------
// HTTP utilities
// ------------------------------------------------------------------
std::string http_ok(const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

// ------------------------------------------------------------------
// Client handler
// ------------------------------------------------------------------
void handle_client(SOCKET c) {
    char buf[4096];
    int n = recv(c, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(c);
        return;
    }
    buf[n] = 0;
    std::string req(buf), resp;

    if (req.find("GET /state") != std::string::npos)
        resp = http_ok(state_json());
    else if (req.find("GET /stats") != std::string::npos)
        resp = http_ok(stats_json());
    else
        resp = http_ok("{\"error\":\"not found\"}");

    send(c, resp.c_str(), (int)resp.size(), 0);
    closesocket(c);
}

// ------------------------------------------------------------------
// MAIN — Corrected initialization for floor 1 = bottom (internal)
// ------------------------------------------------------------------
int main() {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2,2), &w) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gFloors = 5;

        upQ.assign(gFloors + 1, {});
        downQ.assign(gFloors + 1, {});

        pendingUpCall.assign(gFloors + 1, false);
        pendingDownCall.assign(gFloors + 1, false);

        std::vector<int> startFloors = {
            1,
            (gFloors + 1) / 2,
            gFloors
        };

        for (int i = 0; i < 3; ++i) {
            Elevator e;
            e.id = i + 1;
            e.currentFloor = startFloors[i];
            e.targetFloor = e.currentFloor;

            e.direction = 0;
            e.doorOpen = true;
            e.state = ElevatorState::DoorOpen;
            e.stateEndTime = Clock::now() + std::chrono::seconds(5);

            gElevators.push_back(e);
        }
    }

    std::thread(sim_loop).detach();

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(s, (sockaddr*)&addr, sizeof(addr));
    listen(s, 10);

    std::cout << "Sim server running at http://localhost:8080\n";

    while (true) {
        SOCKET c = accept(s, NULL, NULL);
        if (c != INVALID_SOCKET)
            std::thread(handle_client, c).detach();
    }

    closesocket(s);
    WSACleanup();
    return 0;
}