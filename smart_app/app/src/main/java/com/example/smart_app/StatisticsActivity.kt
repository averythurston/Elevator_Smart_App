package com.example.smart_app

import android.graphics.Color
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.SimApi
import com.example.smart_app.data.StatsResponse
import com.github.mikephil.charting.charts.BarChart
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.*
import com.github.mikephil.charting.formatter.IndexAxisValueFormatter
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class StatisticsActivity : AppCompatActivity() {

    private lateinit var tvSummary: TextView
    private lateinit var tvElevatorBreakdown: TextView

    private lateinit var chartUtilization: BarChart
    private lateinit var chartWait: LineChart
    private lateinit var chartEnergy: LineChart

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_statistics)

        tvSummary = findViewById(R.id.tvSummary)
        tvElevatorBreakdown = findViewById(R.id.tvElevatorBreakdown)

        chartUtilization = findViewById(R.id.chartUtilization)
        chartWait = findViewById(R.id.chartWait)
        chartEnergy = findViewById(R.id.chartEnergy)

        formatUtilizationChart()
        formatLineChart(chartWait, "Avg Wait (sec)")
        formatLineChart(chartEnergy, "Energy (kWh)")

        startAutoRefresh()
    }

    private fun startAutoRefresh() {
        lifecycleScope.launch(Dispatchers.IO) {
            while (true) {
                try {
                    val stats = SimApi.api.getStats()
                    withContext(Dispatchers.Main) {
                        updateUI(stats)
                    }
                } catch (_: Exception) {}

                delay(5000) // refresh every 5 seconds
            }
        }
    }

    private fun updateUI(stats: StatsResponse) {

        // -------- SUMMARY (peak hour removed + COST ADDED) --------
        tvSummary.text =
            """
            Floors: ${stats.floorCount}
            Total Trips: ${stats.totalTrips}
            Total Passengers: ${stats.totalPassengers}
            Avg Wait Time: ${"%.1f".format(stats.avgWaitSec)} sec
            Avg Trip Time: ${"%.1f".format(stats.avgTripSec)} sec
            Avg Energy: ${"%.3f".format(stats.avgEnergyKWh)} kWh
            Total Cost: $${"%.4f".format(stats.totalCostCAD)} CAD
            """.trimIndent()

        // -------- PER-ELEVATOR METRICS --------
        val sb = StringBuilder()
        stats.elevators.forEach {
            sb.append(
                """
                Elevator ${it.id}
                  • Trips: ${it.trips}
                  • Passengers: ${it.passengersMoved}
                  • Stops: ${it.stopCount}
                  • Doors Opened: ${it.doorOpenCount}
                  • Energy Used: ${"%.3f".format(it.energyKWh)} kWh

                """.trimIndent()
            )
        }
        tvElevatorBreakdown.text = sb.toString()

        // -------- CHARTS (B, A, C) --------
        updateUtilizationChart(stats)
        updateWaitChart(stats)
        updateEnergyChart(stats)
    }

    // =======================
    // B) Elevator Utilization
    // =======================
    private fun updateUtilizationChart(stats: StatsResponse) {
        val entries = ArrayList<BarEntry>()
        val labels = ArrayList<String>()

        stats.elevators.forEachIndexed { index, e ->
            entries.add(BarEntry(index.toFloat(), e.trips.toFloat()))
            labels.add("E${e.id}")
        }

        val set = BarDataSet(entries, "Trips")
        set.color = Color.rgb(90, 130, 220)

        chartUtilization.data = BarData(set).apply {
            barWidth = 0.5f
        }

        chartUtilization.xAxis.valueFormatter = IndexAxisValueFormatter(labels)
        chartUtilization.xAxis.granularity = 1f

        chartUtilization.invalidate()
    }

    // ===========================
    // A) Avg Wait Time per Hour
    // ===========================
    private fun updateWaitChart(stats: StatsResponse) {
        val entries = ArrayList<Entry>()

        stats.hourly.forEach {
            entries.add(Entry(it.hour.toFloat(), it.avgWaitSec.toFloat()))
        }

        val set = LineDataSet(entries, "Avg Wait (sec)")
        set.color = Color.rgb(70, 170, 110)
        set.lineWidth = 2f
        set.circleRadius = 3f
        set.setCircleColor(Color.rgb(60, 150, 100))
        set.mode = LineDataSet.Mode.CUBIC_BEZIER

        chartWait.data = LineData(set)
        chartWait.invalidate()
    }

    // ===========================
    // C) Energy per Hour
    // ===========================
    private fun updateEnergyChart(stats: StatsResponse) {
        val entries = ArrayList<Entry>()

        stats.hourly.forEach {
            entries.add(Entry(it.hour.toFloat(), it.energyKWh.toFloat()))
        }

        val set = LineDataSet(entries, "Energy (kWh)")
        set.color = Color.rgb(220, 120, 70)
        set.lineWidth = 2f
        set.circleRadius = 3f
        set.setCircleColor(Color.rgb(200, 100, 60))
        set.mode = LineDataSet.Mode.CUBIC_BEZIER

        chartEnergy.data = LineData(set)
        chartEnergy.invalidate()
    }

    // =======================
    // Styling / Format Helpers
    // =======================
    private fun formatUtilizationChart() {
        chartUtilization.axisRight.isEnabled = false
        chartUtilization.description.isEnabled = false
        chartUtilization.legend.isEnabled = false

        chartUtilization.xAxis.position = XAxis.XAxisPosition.BOTTOM
        chartUtilization.xAxis.textSize = 12f
        chartUtilization.axisLeft.textSize = 12f
    }

    private fun formatLineChart(chart: LineChart, label: String) {
        chart.axisRight.isEnabled = false
        chart.description.isEnabled = false
        chart.legend.isEnabled = false

        chart.xAxis.position = XAxis.XAxisPosition.BOTTOM
        chart.xAxis.granularity = 1f
        chart.xAxis.textSize = 12f
        chart.axisLeft.textSize = 12f
    }
}
