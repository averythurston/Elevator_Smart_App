package com.example.smart_app.data

import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import retrofit2.http.GET
import retrofit2.http.Query

// -----------------------------------------
// Data models
// -----------------------------------------

data class ProtoStateResponse(
    val floor: Int,
    val target: Int,
    val dir: Int,
    val door: Int,
    val state: String
)

// Expanded stats model
data class ProtoStats(
    val floor: Int,
    val target: Int,
    val dir: Int,
    val door: Int,
    val state: String,

    val totalTrips: Int = 0,
    val stopCount: Int = 0,
    val doorCycles: Int = 0,
    val avgTripMs: Long = 0,
    val avgWaitMs: Long = 0,
    val travelDistanceFloors: Int = 0,
    val uptimeMs: Long = 0
)

// Optional command response
data class ProtoCommandResponse(
    val success: Boolean,
    val sent: String
)

// -----------------------------------------
// Retrofit service interfaces
// -----------------------------------------

interface PrototypeStateService {
    @GET("state")
    suspend fun getState(): ProtoStateResponse
}

interface PrototypeCommandService {
    @GET("command")
    suspend fun sendCommand(@Query("floor") floor: Int): ProtoCommandResponse
}

interface PrototypeStatsService {
    @GET("stats")
    suspend fun getStats(): ProtoStats
}

// -----------------------------------------
// Singleton API access
// -----------------------------------------

object PrototypeApi {
    private const val BASE = "http://10.0.2.2:8081/"

    private val retrofit: Retrofit by lazy {
        Retrofit.Builder()
            .baseUrl(BASE)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
    }

    val state: PrototypeStateService by lazy {
        retrofit.create(PrototypeStateService::class.java)
    }

    val command: PrototypeCommandService by lazy {
        retrofit.create(PrototypeCommandService::class.java)
    }

    val stats: PrototypeStatsService by lazy {
        retrofit.create(PrototypeStatsService::class.java)
    }
}