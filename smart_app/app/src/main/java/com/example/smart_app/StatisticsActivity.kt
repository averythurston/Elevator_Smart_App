package com.example.smart_app

import android.graphics.Color
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.*
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class StatisticsActivity : AppCompatActivity() {

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
                val stats = withContext(Dispatchers.IO) { SimApi.api.getStats() }
                bindSummary(stats)
                bindElevatorBreakdown(stats.elevators)
                setupTripsChart(stats.hourly)
            } catch (e: Exception) {
                tvSummary.text = "Failed to load statistics."
            }
        }
    }

    private fun bindSummary(s: StatsResponse) {
        tvSummary.text = """
            Floors: ${s.floorCount}
            Total Trips: ${s.totalTrips}
            Total Passengers (spawned): ${s.totalPassengers}
            Peak Hour: ${s.peakHour}:00
            Avg Wait: ${"%.2f".format(s.avgWaitSec)} s
            Avg Trip Time: ${"%.2f".format(s.avgTripSec)} s
            Avg Energy / Trip: ${"%.3f".format(s.avgEnergyKWh)} kWh
        """.trimIndent()
    }

    private fun bindElevatorBreakdown(elevators: List<ElevatorStats>) {
        val sb = StringBuilder()
        elevators.forEach { e ->
            sb.append(
                "Elevator ${e.id} → Trips=${e.trips}, " +
                        "Passengers=${e.passengersMoved}, Stops=${e.stopCount}, " +
                        "Door Opens=${e.doorOpenCount}, Energy=${"%.3f".format(e.energyKWh)} kWh\n"
            )
        }
        tvElevatorBreakdown.text = sb.toString()
    }

    private fun setupTripsChart(hourly: List<HourlyStats>) {
        val entries = hourly.map { Entry(it.hour.toFloat(), it.trips.toFloat()) }

        val ds = LineDataSet(entries, "Trips").apply {
            lineWidth = 2f
            setDrawCircles(true)
            setCircleColor(Color.BLUE)
            color = Color.BLUE
            valueTextSize = 10f
        }

        chartTrips.data = LineData(ds)
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
