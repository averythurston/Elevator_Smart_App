package com.example.smart_app.data

import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import retrofit2.http.GET

// ---------- STATE RESPONSE (/state) ----------

data class StateResponse(
    val floorCount: Int,
    val elevators: List<ElevatorState>
)

data class ElevatorState(
    val id: Int,
    val currentFloor: Int,
    val targetFloor: Int,
    val direction: Int,
    val doorOpen: Boolean,
    val load: Int,
    val capacity: Int,

    // ✅ NEW (from C++)
    val state: String,
    val remainingMs: Long
)

// ---------- STATS RESPONSE (/stats/daily) ----------

data class StatsResponse(
    val floorCount: Int,
    val totalTrips: Int,
    val totalPassengers: Int,
    val avgWaitSec: Double,
    val avgTripSec: Double,
    val avgEnergyKWh: Double,
    val peakHour: Int,
    val elevators: List<ElevatorStats>,
    val hourly: List<HourlyStats>
)

data class ElevatorStats(
    val id: Int,
    val trips: Int,
    val passengersMoved: Int,
    val energyKWh: Double,
    val doorOpenCount: Int,
    val stopCount: Int
)

data class HourlyStats(
    val hour: Int,
    val trips: Int,
    val avgWaitSec: Double,
    val energyKWh: Double
)

// ---------- RETROFIT SERVICE ----------

interface SimService {
    @GET("state")
    suspend fun getState(): StateResponse

    @GET("stats/daily")
    suspend fun getStats(): StatsResponse
}

// ---------- FACTORY + SINGLETON ----------

object SimApi {
    private const val BASE_URL = "http://10.0.2.2:8080/"

    fun create(baseUrl: String = BASE_URL): SimService {
        val retrofit = Retrofit.Builder()
            .baseUrl(baseUrl)
            .addConverterFactory(GsonConverterFactory.create())
            .build()

        return retrofit.create(SimService::class.java)
    }

    val api: SimService by lazy { create(BASE_URL) }
}