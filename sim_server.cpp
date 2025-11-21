// sim_server.cpp — Multi-elevator passenger simulation with real stats.
// Windows build (MinGW):
//   g++ sim_server.cpp -o sim_server -std=c++17 -lws2_32
// Run:
//   .\sim_server
//
// Endpoints:
//   GET /state
//   GET /stats/daily

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
    int direction;     // +1 up, -1 down
    TimePoint created;
};

enum class ElevatorState { Idle, Moving, DoorOpen };

struct Elevator {
    int id;
    int currentFloor;
    int targetFloor;
    int direction; // +1 up, -1 down, 0 idle
    bool doorOpen;
    ElevatorState state;
    TimePoint stateEndTime;

    int capacity = 10;
    std::vector<Passenger> onboard;

    // per-elevator stats
    int trips = 0;
    int passengersMoved = 0;
    double energyKWh = 0.0;
    int doorOpenCount = 0;
    int stopCount = 0;
};

struct HourlyBucket {
    int trips = 0;
    double energyKWh = 0.0;
    double totalWaitSec = 0.0;
    int waitCount = 0;
};

struct GlobalStats {
    int totalTrips = 0;
    int totalPassengers = 0;
    int completedPassengers = 0;

    double totalEnergyKWh = 0.0;
    double totalWaitSec = 0.0;
    double totalTripSec = 0.0;
    int completedTrips = 0;
};

int gFloors = 5;
std::vector<Elevator> gElevators;
std::vector<std::deque<Passenger>> upQ, downQ;
GlobalStats gStats;
HourlyBucket gHourly[24];
std::mutex gMutex;

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

Passenger make_passenger(int floor) {
    std::uniform_int_distribution<int> dist(1, gFloors);
    int dest = floor;
    while (dest == floor) dest = dist(rng());

    Passenger p;
    p.startFloor = floor;
    p.destFloor = dest;
    p.direction = (dest > floor ? +1 : -1);
    p.created = Clock::now();
    return p;
}

void generate_traffic() {
    int h = fake_hour();
    double rateMin = spawn_rate_per_min(h);
    double rateSec = rateMin / 60.0;

    for (int f = 1; f <= gFloors; ++f) {
        if (should_spawn(rateSec)) {
            Passenger p = make_passenger(f);
            if (p.direction == +1) upQ[f].push_back(p);
            else downQ[f].push_back(p);
            gStats.totalPassengers++;
        }
    }
}

int choose_next_target(const Elevator& e) {
    if (!e.onboard.empty())
        return e.onboard.front().destFloor;

    int best = e.currentFloor;
    int bestDist = 999;

    for (int f = 1; f <= gFloors; ++f) {
        if (upQ[f].empty() && downQ[f].empty()) continue;
        int d = std::abs(f - e.currentFloor);
        if (d < bestDist) { bestDist = d; best = f; }
    }
    return best;
}

void update_elevator(Elevator& e, TimePoint now) {
    using namespace std::chrono;

    if (e.state == ElevatorState::Idle) {
        if (now >= e.stateEndTime) {
            int next = choose_next_target(e);
            if (next == e.currentFloor) {
                e.direction = 0;
                e.stateEndTime = now + seconds(1);
                return;
            }

            e.targetFloor = next;
            int diff = e.targetFloor - e.currentFloor;
            int floors = std::abs(diff);

            e.direction = diff > 0 ? +1 : -1;
            e.doorOpen = false;
            e.state = ElevatorState::Moving;

            double tSec = travel_time_sec(floors);
            e.stateEndTime = now + duration_cast<Clock::duration>(duration<double>(tSec));

            // trip stats
            gStats.totalTrips++;
            gStats.completedTrips++;
            gStats.totalTripSec += tSec;
            e.trips++;

            int h = fake_hour();
            gHourly[h].trips++;
        }
    }
    else if (e.state == ElevatorState::Moving) {
        if (now >= e.stateEndTime) {
            int diff = std::abs(e.targetFloor - e.currentFloor);
            double tSec = travel_time_sec(diff);

            double loadFactor = 1.0 + 0.05 * e.onboard.size();
            double energy = 0.05 * diff * loadFactor;

            gStats.totalEnergyKWh += energy;
            e.energyKWh += energy;
            gHourly[fake_hour()].energyKWh += energy;

            e.currentFloor = e.targetFloor;
            e.direction = 0;
            e.doorOpen = true;
            e.state = ElevatorState::DoorOpen;
            e.stateEndTime = now + seconds(5); // doors open/close timing

            e.stopCount++;
            e.doorOpenCount++;

            // exit
            auto it = e.onboard.begin();
            while (it != e.onboard.end()) {
                if (it->destFloor == e.currentFloor) {
                    gStats.completedPassengers++;
                    e.passengersMoved++;
                    it = e.onboard.erase(it);
                } else ++it;
            }

            // enter
            int capLeft = e.capacity - (int)e.onboard.size();
            auto& U = upQ[e.currentFloor];
            auto& D = downQ[e.currentFloor];

            auto board = [&](std::deque<Passenger>& q) {
                while (capLeft > 0 && !q.empty()) {
                    Passenger p = q.front(); q.pop_front();
                    double waitSec = duration<double>(now - p.created).count();

                    gStats.totalWaitSec += waitSec;
                    int h2 = fake_hour();
                    gHourly[h2].totalWaitSec += waitSec;
                    gHourly[h2].waitCount++;

                    e.onboard.push_back(p);
                    capLeft--;
                }
            };

            board(U);
            board(D);
        }
    }
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
            for (auto& e : gElevators)
                update_elevator(e, now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ✅ UPDATED: /state now includes state + remainingMs
std::string state_json() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::ostringstream out;

    auto now = Clock::now();

    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"elevators\":[";

    for (size_t i = 0; i < gElevators.size(); ++i) {
        const auto& e = gElevators[i];
        if (i) out << ",";

        long long remainingMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(e.stateEndTime - now).count();
        if (remainingMs < 0) remainingMs = 0;

        std::string stateStr;
        switch (e.state) {
            case ElevatorState::Idle:     stateStr = "Idle"; break;
            case ElevatorState::Moving:   stateStr = "Moving"; break;
            case ElevatorState::DoorOpen: stateStr = "DoorOpen"; break;
        }

        out << "{"
            << "\"id\":" << e.id
            << ",\"currentFloor\":" << e.currentFloor
            << ",\"targetFloor\":" << e.targetFloor
            << ",\"direction\":" << e.direction
            << ",\"doorOpen\":" << (e.doorOpen ? "true" : "false")
            << ",\"load\":" << e.onboard.size()
            << ",\"capacity\":" << e.capacity
            << ",\"state\":\"" << stateStr << "\""
            << ",\"remainingMs\":" << remainingMs
            << "}";
    }

    out << "]}";
    return out.str();
}

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
        if (gHourly[h].trips > maxTrips) { maxTrips = gHourly[h].trips; peakHour = h; }

    std::ostringstream out;
    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"totalTrips\":" << gStats.totalTrips << ",";
    out << "\"totalPassengers\":" << gStats.totalPassengers << ",";
    out << "\"avgWaitSec\":" << avgWait << ",";
    out << "\"avgTripSec\":" << avgTrip << ",";
    out << "\"avgEnergyKWh\":" << avgEnergy << ",";
    out << "\"peakHour\":" << peakHour << ",";

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

std::string http_ok(const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

void handle_client(SOCKET c) {
    char buf[4096];
    int n = recv(c, buf, sizeof(buf)-1, 0);
    if (n <= 0) { closesocket(c); return; }
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

        for (int i = 0; i < 3; ++i) {
            Elevator e;
            e.id = i + 1;
            e.currentFloor = i + 1;
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

    std::cout << "Sim server at http://localhost:8080\n";

    while (true) {
        SOCKET c = accept(s, NULL, NULL);
        if (c != INVALID_SOCKET)
            std::thread(handle_client, c).detach();
    }

    closesocket(s);
    WSACleanup();
    return 0;
}
