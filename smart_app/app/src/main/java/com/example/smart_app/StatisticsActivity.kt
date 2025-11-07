package com.example.smart_app

import android.graphics.Color
import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.github.mikephil.charting.charts.BarChart
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.Description
import com.github.mikephil.charting.data.*
import com.github.mikephil.charting.formatter.IndexAxisValueFormatter

class StatisticsActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_statistics)

        // --- Get references to views ---
        val barChart = findViewById<BarChart>(R.id.barChart)
        val tvTotalTrips = findViewById<TextView>(R.id.tvTotalTrips)
        val tvAvgWait = findViewById<TextView>(R.id.tvAvgWait)
        val tvUptime = findViewById<TextView>(R.id.tvUptime)
        val tvEnergy = findViewById<TextView>(R.id.tvEnergy)
        val tvSummary = findViewById<TextView>(R.id.tvSummary)

        // Optional line chart (if you added it in XML)
        val lineChart = try {
            findViewById<LineChart>(R.id.lineChart)
        } catch (e: Exception) {
            null
        }

        // --- Simulated data: Elevator trips by time of day ---
        val timeSlots = listOf("6-9AM", "9-12PM", "12-3PM", "3-6PM", "6-9PM", "9-12AM")
        val tripsPerSlot = listOf(40f, 70f, 55f, 80f, 35f, 20f)

        // --- Calculated metrics ---
        val totalTrips = tripsPerSlot.sum().toInt()
        val peakIndex = tripsPerSlot.indexOf(tripsPerSlot.maxOrNull()!!)
        val peakTime = timeSlots[peakIndex]
        val avgWaitTime = 9.2 // seconds
        val uptime = 99.4 // percent
        val avgEnergy = 11.7 // kWh

        // --- Update summary fields ---
        tvTotalTrips.text = "$totalTrips"
        tvAvgWait.text = "${avgWaitTime}s"
        tvUptime.text = "$uptime%"
        tvEnergy.text = "$avgEnergy"
        tvSummary.text =
            "Summary: $totalTrips total trips today. Peak activity during $peakTime. Uptime $uptime%."

        // --- Build bar chart (Trips per Time of Day) ---
        val barEntries = tripsPerSlot.mapIndexed { index, value ->
            BarEntry(index.toFloat(), value)
        }

        val barDataSet = BarDataSet(barEntries, "Trips per Time of Day")
        barDataSet.color = Color.parseColor("#4CAF50")
        barDataSet.valueTextColor = Color.BLACK
        barDataSet.valueTextSize = 12f

        val barData = BarData(barDataSet)
        barChart.data = barData

        barChart.setFitBars(true)
        barChart.setDrawGridBackground(false)
        barChart.axisRight.isEnabled = false
        barChart.axisLeft.textColor = Color.DKGRAY
        barChart.xAxis.valueFormatter = IndexAxisValueFormatter(timeSlots)
        barChart.xAxis.textColor = Color.DKGRAY
        barChart.xAxis.granularity = 1f
        barChart.xAxis.labelRotationAngle = -30f
        barChart.legend.textColor = Color.BLACK
        barChart.description = Description().apply { text = "" }
        barChart.animateY(1200)
        barChart.invalidate()

        // --- Optional: Line Chart (Daily Trend) ---
        lineChart?.let {
            val lineEntries = tripsPerSlot.mapIndexed { index, value ->
                Entry(index.toFloat(), value)
            }

            val lineDataSet = LineDataSet(lineEntries, "Hourly Usage Trend")
            lineDataSet.color = Color.parseColor("#2196F3")
            lineDataSet.circleRadius = 4f
            lineDataSet.setCircleColor(Color.parseColor("#2196F3"))
            lineDataSet.valueTextColor = Color.BLACK
            lineDataSet.lineWidth = 2f

            val lineData = LineData(lineDataSet)
            it.data = lineData

            it.xAxis.valueFormatter = IndexAxisValueFormatter(timeSlots)
            it.xAxis.labelRotationAngle = -30f
            it.axisRight.isEnabled = false
            it.axisLeft.textColor = Color.DKGRAY
            it.legend.textColor = Color.BLACK
            it.description = Description().apply { text = "" }
            it.animateY(1200)
            it.invalidate()
        }
    }
}