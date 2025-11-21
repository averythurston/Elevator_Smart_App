// sim_server.cpp — Full elevator + passenger traffic simulation with stats
// Build on Windows (MinGW):
//   g++ sim_server.cpp -o sim_server -std=c++17 -lws2_32
//
// Run:
//   .\sim_server
//
// Android emulator base URL:
//   http://10.0.2.2:8080/

#include <iostream>
#include <thread>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>
#include <string>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// -----------------------------
// DATA STRUCTURES
// -----------------------------

struct Passenger {
    int startFloor;
    int destFloor;
    int direction;      // +1 up, -1 down
    TimePoint created;  // when they arrived at the floor
};

enum class ElevatorState {
    Idle,
    Moving,
    DoorOpen
};

struct Elevator {
    int id;
    int currentFloor;
    int targetFloor;
    int direction;          // +1 up, -1 down, 0 idle
    bool doorOpen;
    ElevatorState state;
    TimePoint stateEndTime;

    int capacity = 10;
    std::vector<Passenger> onboard;

    // Per-elevator stats
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
    int totalPassengers = 0;        // spawned
    int completedPassengers = 0;    // actually moved

    double totalEnergyKWh = 0.0;
    double totalWaitSec = 0.0;
    double totalTripSec = 0.0;
    int completedTrips = 0;
};

int gFloors = 5;
std::vector<Elevator> gElevators;
std::vector<std::deque<Passenger>> floorUpQueue;
std::vector<std::deque<Passenger>> floorDownQueue;
GlobalStats gStats;
HourlyBucket gHourly[24];

std::mutex gMutex;

std::mt19937& rng() {
    static std::mt19937 gen{ std::random_device{}() };
    return gen;
}

// -----------------------------
// TIMING HELPERS
// -----------------------------

// Realistic move timing:
// 1 floor: 7.5 s
// 2 floors: 7.5 + 7.5 = 15 s
// >=3 floors: 7.5 + 7.5 + 7.0*(floors-2)
double compute_travel_time_sec(int floors) {
    if (floors <= 1) return 7.5;
    return 7.5 + 7.5 + 7.0 * (floors - 2);
}

// Fake "hour of day" based on simulation time
int get_fake_hour() {
    auto now = Clock::now().time_since_epoch();
    long seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    // Every 30 seconds of real time = 1 simulated hour
    return static_cast<int>((seconds / 30) % 24);
}

// -----------------------------
// TRAFFIC GENERATION
// -----------------------------

double spawn_rate_for_hour(int hour) {
    if (hour >= 7 && hour < 10) return 0.25;  // morning rush
    if (hour >= 11 && hour < 14) return 0.15; // lunch
    if (hour >= 16 && hour < 19) return 0.30; // evening rush
    return 0.05;                              // low
}

bool should_spawn(double ratePerSecond) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng()) < ratePerSecond;
}

Passenger create_passenger(int floor) {
    std::uniform_int_distribution<int> dist(1, gFloors);
    int dest = floor;
    while (dest == floor) dest = dist(rng());

    Passenger p;
    p.startFloor = floor;
    p.destFloor  = dest;
    p.direction  = (dest > floor ? +1 : -1);
    p.created    = Clock::now();
    return p;
}

void generate_passenger_traffic() {
    using namespace std::chrono;

    int hour         = get_fake_hour();
    double ratePerMin = spawn_rate_for_hour(hour);
    double ratePerSec = ratePerMin / 60.0;

    for (int f = 1; f <= gFloors; ++f) {
        if (should_spawn(ratePerSec)) {
            Passenger p = create_passenger(f);

            if (p.direction == +1)
                floorUpQueue[f].push_back(p);
            else
                floorDownQueue[f].push_back(p);

            gStats.totalPassengers++;
        }
    }
}

// -----------------------------
// DISPATCH LOGIC
// -----------------------------

int choose_next_target_for_elevator(const Elevator& e) {
    if (!e.onboard.empty()) {
        return e.onboard.front().destFloor;
    }

    int current = e.currentFloor;
    int bestFloor = -1;
    int bestDist  = 999;

    for (int f = 1; f <= gFloors; ++f) {
        bool hasQueue = !floorUpQueue[f].empty() || !floorDownQueue[f].empty();
        if (!hasQueue) continue;

        int dist = std::abs(f - current);
        if (dist < bestDist) {
            bestDist  = dist;
            bestFloor = f;
        }
    }

    if (bestFloor != -1) return bestFloor;

    return current;
}

// -----------------------------
// ELEVATOR UPDATE
// -----------------------------

void update_elevator(Elevator& e, TimePoint now) {
    using namespace std::chrono;

    if (e.state == ElevatorState::Idle) {
        if (now >= e.stateEndTime) {
            int nextFloor = choose_next_target_for_elevator(e);

            if (nextFloor == e.currentFloor) {
                e.direction    = 0;
                e.doorOpen     = false;
                e.state        = ElevatorState::Idle;
                e.stateEndTime = now + seconds(1);
                return;
            }

            e.targetFloor = nextFloor;
            int diff      = e.targetFloor - e.currentFloor;
            int floors    = std::abs(diff);

            e.direction = (diff > 0 ? +1 : -1);
            e.doorOpen  = false;
            e.state     = ElevatorState::Moving;

            double travelSec = compute_travel_time_sec(floors);
            auto travelDur   = duration_cast<Clock::duration>(duration<double>(travelSec));
            e.stateEndTime   = now + travelDur;

            // Trip-level stats
            gStats.totalTrips++;
            gStats.completedTrips++;
            gStats.totalTripSec += travelSec;
            e.trips++;

            int hour = get_fake_hour();
            gHourly[hour].trips++;
        }

    } else if (e.state == ElevatorState::Moving) {
        if (now >= e.stateEndTime) {
            int diff = std::abs(e.targetFloor - e.currentFloor);
            double sec = compute_travel_time_sec(diff);

            double loadFactor = 1.0 + 0.05 * static_cast<double>(e.onboard.size());
            double energy = 0.05 * diff * loadFactor;

            gStats.totalEnergyKWh += energy;
            e.energyKWh           += energy;

            int hour = get_fake_hour();
            gHourly[hour].energyKWh += energy;

            e.currentFloor = e.targetFloor;
            e.direction    = 0;
            e.doorOpen     = true;
            e.state        = ElevatorState::DoorOpen;
            e.stateEndTime = now + seconds(5);

            e.stopCount++;
            e.doorOpenCount++;

            // Passengers EXIT
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

            // Passengers ENTER
            int capLeft = e.capacity - static_cast<int>(e.onboard.size());

            auto& upQ = floorUpQueue[e.currentFloor];
            while (capLeft > 0 && !upQ.empty()) {
                Passenger p = upQ.front(); upQ.pop_front();
                double waitSec =
                    duration<double>(now - p.created).count();
                gStats.totalWaitSec += waitSec;

                int hour2 = get_fake_hour();
                gHourly[hour2].totalWaitSec += waitSec;
                gHourly[hour2].waitCount++;

                e.onboard.push_back(p);
                e.passengersMoved++;
                gStats.completedPassengers++;
                capLeft--;
            }

            auto& downQ = floorDownQueue[e.currentFloor];
            while (capLeft > 0 && !downQ.empty()) {
                Passenger p = downQ.front(); downQ.pop_front();
                double waitSec =
                    duration<double>(now - p.created).count();
                gStats.totalWaitSec += waitSec;

                int hour2 = get_fake_hour();
                gHourly[hour2].totalWaitSec += waitSec;
                gHourly[hour2].waitCount++;

                e.onboard.push_back(p);
                e.passengersMoved++;
                gStats.completedPassengers++;
                capLeft--;
            }
        }

    } else if (e.state == ElevatorState::DoorOpen) {
        if (now >= e.stateEndTime) {
            e.doorOpen = false;
            e.state    = ElevatorState::Idle;
            e.stateEndTime = now + std::chrono::seconds(1);
        }
    }
}

// -----------------------------
// SIMULATION LOOP
// -----------------------------

void simulation_loop() {
    using namespace std::chrono;

    while (true) {
        auto now = Clock::now();

        {
            std::lock_guard<std::mutex> lock(gMutex);

            generate_passenger_traffic();

            for (auto& e : gElevators) {
                update_elevator(e, now);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// -----------------------------
// JSON BUILDERS
// -----------------------------

std::string build_state_json() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::ostringstream out;

    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"elevators\":[";

    for (size_t i = 0; i < gElevators.size(); ++i) {
        const auto& e = gElevators[i];
        if (i > 0) out << ",";
        out << "{"
            << "\"id\":" << e.id
            << ",\"currentFloor\":" << e.currentFloor
            << ",\"targetFloor\":" << e.targetFloor
            << ",\"direction\":" << e.direction
            << ",\"doorOpen\":" << (e.doorOpen ? "true" : "false")
            << ",\"load\":" << e.onboard.size()
            << ",\"capacity\":" << e.capacity
            << "}";
    }

    out << "]}";
    return out.str();
}

std::string build_stats_json() {
    std::lock_guard<std::mutex> lock(gMutex);

    int totalTrips   = gStats.totalTrips;
    int totalPass    = gStats.totalPassengers;

    double avgWaitSec =
        (gStats.completedPassengers > 0)
            ? (gStats.totalWaitSec / gStats.completedPassengers)
            : 5.0;

    double avgTripSec =
        (gStats.completedTrips > 0)
            ? (gStats.totalTripSec / gStats.completedTrips)
            : 15.0;

    double avgEnergy =
        (totalTrips > 0)
            ? (gStats.totalEnergyKWh / totalTrips)
            : 0.2;

    int peakHour = 0;
    int maxTrips = 0;
    for (int h = 0; h < 24; ++h) {
        if (gHourly[h].trips > maxTrips) {
            maxTrips = gHourly[h].trips;
            peakHour = h;
        }
    }

    std::ostringstream out;
    out << "{";
    out << "\"floorCount\":" << gFloors << ",";
    out << "\"totalTrips\":" << totalTrips << ",";
    out << "\"totalPassengers\":" << totalPass << ",";
    out << "\"avgWaitSec\":" << avgWaitSec << ",";
    out << "\"avgTripSec\":" << avgTripSec << ",";
    out << "\"avgEnergyKWh\":" << avgEnergy << ",";
    out << "\"peakHour\":" << peakHour << ",";

    // Per-elevator stats
    out << "\"elevators\":[";
    for (size_t i = 0; i < gElevators.size(); ++i) {
        const auto& e = gElevators[i];
        if (i > 0) out << ",";
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

    // Hourly stats array
    out << "\"hourly\":[";
    for (int h = 0; h < 24; ++h) {
        if (h > 0) out << ",";
        double avgHWait =
            (gHourly[h].waitCount > 0)
                ? (gHourly[h].totalWaitSec / gHourly[h].waitCount)
                : 0.0;
        out << "{"
            << "\"hour\":" << h
            << ",\"trips\":" << gHourly[h].trips
            << ",\"avgWaitSec\":" << avgHWait
            << ",\"energyKWh\":" << gHourly[h].energyKWh
            << "}";
    }
    out << "]}";

    return out.str();
}

// -----------------------------
// HTTP UTILITIES
// -----------------------------

std::string http_ok(const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

void handle_client(SOCKET client) {
    char buffer[4096];
    int n = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return;
    }
    buffer[n] = '\0';

    std::string req(buffer);
    std::string resp;

    if (req.find("GET /state") != std::string::npos) {
        resp = http_ok(build_state_json());
    } else if (req.find("GET /stats/daily") != std::string::npos ||
               req.find("GET /stats") != std::string::npos) {
        resp = http_ok(build_stats_json());
    } else {
        resp = http_ok("{\"error\":\"not found\"}");
    }

    send(client, resp.c_str(), (int)resp.size(), 0);
    closesocket(client);
}

// -----------------------------
// MAIN
// -----------------------------

int main() {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(gMutex);

        gFloors = 5;
        floorUpQueue.assign(gFloors + 1, {});
        floorDownQueue.assign(gFloors + 1, {});

        for (int i = 0; i < 3; ++i) {
            Elevator e;
            e.id           = i + 1;
            e.currentFloor = i + 1;
            e.targetFloor  = e.currentFloor;
            e.direction    = 0;
            e.doorOpen     = true;
            e.state        = ElevatorState::DoorOpen;
            e.stateEndTime = Clock::now() + std::chrono::seconds(5);
            e.capacity     = 10;
            gElevators.push_back(e);
        }
    }

    std::thread(simulation_loop).detach();

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (listen(sock, 10) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Simulation server running at http://localhost:8080\n";

    while (true) {
        SOCKET client = accept(sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        std::thread(handle_client, client).detach();
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
