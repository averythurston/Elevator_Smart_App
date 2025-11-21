package com.example.smart_app

import android.graphics.Color
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.*
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class StatisticsActivity : AppCompatActivity() {

    private val api by lazy {
        SimApi.create("http://10.0.2.2:8080/")
    }

    private lateinit var chartTrips: LineChart
    private lateinit var tvSummary: TextView
    private lateinit var tvElevatorBreakdown: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_statistics)

        chartTrips = findViewById(R.id.chartTrips)
        tvSummary = findViewById(R.id.tvSummary)
        tvElevatorBreakdown = findViewById(R.id.tvElevatorBreakdown)

        loadStats()
    }

    private fun loadStats() {
        lifecycleScope.launch {
            try {
                val stats = withContext(Dispatchers.IO) { api.getStats() }
                bindSummary(stats)
                bindElevatorBreakdown(stats.elevators)
                setupTripsChart(stats.hourly)
            } catch (e: Exception) {
                tvSummary.text = "Failed to load statistics from simulation."
            }
        }
    }

    private fun bindSummary(s: StatsResponse) {
        val avgWait = String.format("%.2f", s.avgWaitSec)
        val avgTrip = String.format("%.2f", s.avgTripSec)
        val avgEnergy = String.format("%.3f", s.avgEnergyKWh)

        tvSummary.text = """
            Floors: ${s.floorCount}
            Total Trips: ${s.totalTrips}
            Total Passengers (spawned): ${s.totalPassengers}
            Peak Hour: ${s.peakHour}:00
            Avg Wait: $avgWait s
            Avg Trip Time: $avgTrip s
            Avg Energy / Trip: $avgEnergy kWh
        """.trimIndent()
    }

    private fun bindElevatorBreakdown(elevators: List<ElevatorStats>) {
        val builder = StringBuilder()
        for (e in elevators) {
            val energy = String.format("%.3f", e.energyKWh)
            builder.append(
                "Elevator ${e.id} → " +
                        "Trips=${e.trips}, " +
                        "Passengers=${e.passengersMoved}, " +
                        "Stops=${e.stopCount}, " +
                        "Door Opens=${e.doorOpenCount}, " +
                        "Energy=${energy} kWh\n"
            )
        }
        tvElevatorBreakdown.text = builder.toString()
    }

    private fun setupTripsChart(hourly: List<HourlyStats>) {
        val entries = hourly.map {
            Entry(it.hour.toFloat(), it.trips.toFloat())
        }

        val dataSet = LineDataSet(entries, "Trips per Hour").apply {
            lineWidth = 2f
            setDrawCircles(true)
            setCircleColor(Color.BLUE)
            color = Color.BLUE
            valueTextSize = 10f
        }

        chartTrips.data = LineData(dataSet)

        chartTrips.xAxis.apply {
            position = XAxis.XAxisPosition.BOTTOM
            granularity = 1f
            labelCount = 6
        }

        chartTrips.axisRight.isEnabled = false
        chartTrips.description.isEnabled = false
        chartTrips.invalidate()
    }
}
