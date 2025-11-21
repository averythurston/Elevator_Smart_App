package com.example.smart_app

import android.animation.ArgbEvaluator
import android.animation.ObjectAnimator
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.SimApi
import kotlinx.coroutines.*
import kotlin.math.abs

class VisualSimulationActivity : AppCompatActivity() {

    private lateinit var elevator1: TextView
    private lateinit var elevator2: TextView
    private lateinit var elevator3: TextView

    private lateinit var tvBuilding: TextView
    private lateinit var floorsContainer: LinearLayout
    private lateinit var tvCurrentFloor: TextView
    private lateinit var tvLoadStatus: TextView

    private lateinit var shaft1: FrameLayout
    private lateinit var shaft2: FrameLayout
    private lateinit var shaft3: FrameLayout

    private val floorPositions = mutableListOf<Float>()
    private var floorCount = 5

    private var pollJob: Job? = null
    private val currentFloorIndices = intArrayOf(0, 0, 0)

    private val api by lazy {
        SimApi.create("http://10.0.2.2:8080/")
    }

    private val floorHeightDp = 80f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_visual_simulation)

        // UI refs
        elevator1 = findViewById(R.id.elevator1)
        elevator2 = findViewById(R.id.elevator2)
        elevator3 = findViewById(R.id.elevator3)

        tvBuilding = findViewById(R.id.tvBuilding)
        floorsContainer = findViewById(R.id.floorsContainer)
        tvCurrentFloor = findViewById(R.id.tvCurrentFloor)
        tvLoadStatus = findViewById(R.id.tvLoadStatus)

        shaft1 = findViewById(R.id.shaftContainer1)
        shaft2 = findViewById(R.id.shaftContainer2)
        shaft3 = findViewById(R.id.shaftContainer3)

        val btnStats = findViewById<Button>(R.id.btnStats)

        // Get mode + floors from Login
        floorCount = intent.getIntExtra("floorCount", 5)
        val mode = intent.getStringExtra("mode") ?: "simulation"
        tvBuilding.text = if (mode == "simulation") {
            "Simulation Mode — $floorCount Floors"
        } else {
            "Prototype Mode — $floorCount Floors"
        }

        // Build floors + shafts once layout is ready
        shaft1.post {
            val height = floorCount * dpToPx(floorHeightDp)
            buildFloorLabels(height)
            buildTicks(height)
            computeFloorPositions()
        }

        btnStats.setOnClickListener {
            startActivity(Intent(this, StatisticsActivity::class.java))
        }
    }

    // -------- Floor labels --------
    private fun buildFloorLabels(height: Float) {
        floorsContainer.removeAllViews()
        floorsContainer.layoutParams.height = height.toInt()

        for (i in floorCount downTo 1) {
            val label = TextView(this)
            label.text = "Floor $i"
            label.textSize = 16f
            label.gravity = Gravity.CENTER_VERTICAL
            label.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dpToPx(floorHeightDp).toInt()
            )
            floorsContainer.addView(label)
        }
    }

    // -------- Tick marks + elevator placement --------
    private fun buildTicks(height: Float) {
        val shafts = listOf(
            shaft1 to elevator1,
            shaft2 to elevator2,
            shaft3 to elevator3
        )

        for ((shaft, car) in shafts) {
            shaft.removeAllViews()
            shaft.layoutParams.height = height.toInt()

            // horizontal ticks for each floor
            for (i in 0 until floorCount) {
                val tick = View(this)
                tick.setBackgroundColor(Color.DKGRAY)
                val params = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    dpToPx(2f).toInt()
                )
                tick.y = height - dpToPx(floorHeightDp) * (i + 1)
                shaft.addView(tick, params)
            }

            // add the elevator
            shaft.addView(car)
        }
    }

    // -------- Precompute Y positions per floor --------
    private fun computeFloorPositions() {
        floorPositions.clear()

        val totalHeight = (floorCount - 1) * dpToPx(floorHeightDp)
        for (i in 0 until floorCount) {
            val y = totalHeight - i * dpToPx(floorHeightDp)
            floorPositions.add(y)
        }

        elevator1.translationY = floorPositions[0]
        elevator2.translationY = floorPositions[0]
        elevator3.translationY = floorPositions[0]

        currentFloorIndices.fill(0)
        updateCurrentFloorLabel()
    }

    // -------- Animate an elevator to a floor index --------
    private fun moveElevatorToFloor(view: TextView, index: Int, targetIndex: Int) {
        val current = currentFloorIndices[index]
        if (current == targetIndex) return

        val floorsToMove = abs(targetIndex - current)

        // same timing logic we used before
        val duration = when (floorsToMove) {
            1 -> 7500L
            else -> 7500L + (floorsToMove - 2) * 7000L + 7500L
        }

        val anim = ObjectAnimator.ofFloat(
            view,
            "translationY",
            floorPositions[current],
            floorPositions[targetIndex]
        )
        anim.duration = duration
        anim.start()

        currentFloorIndices[index] = targetIndex
        updateCurrentFloorLabel()
        flashElevator(view)
    }

    private fun flashElevator(view: TextView) {
        val flash = ObjectAnimator.ofInt(
            view,
            "backgroundColor",
            Color.parseColor("#555555"),
            Color.parseColor("#AAAAAA"),
            Color.parseColor("#555555")
        )
        flash.duration = 600
        flash.setEvaluator(ArgbEvaluator())
        flash.start()
    }

    private fun View.isAnimating(): Boolean = this.animation != null

    // -------- Poll simulation server --------
    override fun onStart() {
        super.onStart()

        pollJob = lifecycleScope.launch {
            while (isActive) {
                try {
                    val state = withContext(Dispatchers.IO) { api.getState() }
                    val elevators = state.elevators

                    // map floors to indices (0 = bottom)
                    elevators.getOrNull(0)?.let {
                        val idx = (floorCount - it.currentFloor).coerceIn(0, floorCount - 1)
                        if (!elevator1.isAnimating()) moveElevatorToFloor(elevator1, 0, idx)
                    }

                    elevators.getOrNull(1)?.let {
                        val idx = (floorCount - it.currentFloor).coerceIn(0, floorCount - 1)
                        if (!elevator2.isAnimating()) moveElevatorToFloor(elevator2, 1, idx)
                    }

                    elevators.getOrNull(2)?.let {
                        val idx = (floorCount - it.currentFloor).coerceIn(0, floorCount - 1)
                        if (!elevator3.isAnimating()) moveElevatorToFloor(elevator3, 2, idx)
                    }

                    // load display
                    val e1 = elevators.getOrNull(0)
                    val e2 = elevators.getOrNull(1)
                    val e3 = elevators.getOrNull(2)

                    tvLoadStatus.text =
                        "Load:  E1 ${e1?.load ?: 0}/${e1?.capacity ?: 10}   |   " +
                                "E2 ${e2?.load ?: 0}/${e2?.capacity ?: 10}   |   " +
                                "E3 ${e3?.load ?: 0}/${e3?.capacity ?: 10}"

                } catch (_: Exception) {
                    // ignore for now to keep UI simple
                }

                delay(1000)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        pollJob?.cancel()
    }

    private fun updateCurrentFloorLabel() {
        val e1 = floorCount - currentFloorIndices[0]
        val e2 = floorCount - currentFloorIndices[1]
        val e3 = floorCount - currentFloorIndices[2]
        tvCurrentFloor.text = "Current Floors →  E1: $e1 | E2: $e2 | E3: $3"
    }

    private fun dpToPx(dp: Float): Float =
        dp * resources.displayMetrics.density
}

