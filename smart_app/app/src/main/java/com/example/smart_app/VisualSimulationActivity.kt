package com.example.smart_app

import android.content.Intent
import android.graphics.Color
import android.os.*
import android.view.Gravity
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.*
import kotlinx.coroutines.*
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

class VisualSimulationActivity : AppCompatActivity() {

    private lateinit var tvBuilding: TextView
    private lateinit var tvCurrentFloors: TextView
    private lateinit var tvLoad: TextView

    private lateinit var floorLabels: LinearLayout

    private lateinit var shaft1: FrameLayout
    private lateinit var shaft2: FrameLayout
    private lateinit var shaft3: FrameLayout

    private lateinit var elevator1: TextView
    private lateinit var elevator2: TextView
    private lateinit var elevator3: TextView

    private val floorHeightDp = 80f
    private var floorCount = 5

    private var pollingJob: Job? = null
    private var initializedLayout = false

    private lateinit var mode: String

    private val shafts by lazy { listOf(shaft1, shaft2, shaft3) }
    private val elevators by lazy { listOf(elevator1, elevator2, elevator3) }

    // ---------- Smooth interpolation state ----------
    private data class MoveTrack(
        var isMoving: Boolean = false,
        var startFloor: Int = 1,
        var targetFloor: Int = 1,
        var totalTripMs: Long = 0L,
        var remainingMsReported: Long = 0L,
        var lastServerUpdateMs: Long = 0L
    )

    private val tracks = arrayOf(MoveTrack(), MoveTrack(), MoveTrack())
    private var latestStates: List<ElevatorState> = emptyList()

    private val uiHandler = Handler(Looper.getMainLooper())
    private val frameRunnable = object : Runnable {
        override fun run() {
            renderFrame()
            uiHandler.postDelayed(this, 16L) // ~60fps
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_visual_simulation)

        mode = intent.getStringExtra("mode") ?: "simulation"

        tvBuilding = findViewById(R.id.tvBuilding)
        tvCurrentFloors = findViewById(R.id.tvCurrentFloors)
        tvLoad = findViewById(R.id.tvLoad)

        floorLabels = findViewById(R.id.floorLabels)

        shaft1 = findViewById(R.id.shaftContainer1)
        shaft2 = findViewById(R.id.shaftContainer2)
        shaft3 = findViewById(R.id.shaftContainer3)

        elevator1 = findViewById(R.id.elevator1)
        elevator2 = findViewById(R.id.elevator2)
        elevator3 = findViewById(R.id.elevator3)

        // Prototype mode → hide unused elevators
        if (mode == "prototype") {
            shaft2.visibility = View.GONE
            shaft3.visibility = View.GONE
        }

        findViewById<Button>(R.id.btnStats).setOnClickListener {
            startActivity(Intent(this, StatisticsActivity::class.java))
        }

        startPolling()
        uiHandler.post(frameRunnable)
    }

    override fun onDestroy() {
        super.onDestroy()
        pollingJob?.cancel()
        uiHandler.removeCallbacks(frameRunnable)
    }

    // ---------- Poll correct server ----------
    private fun startPolling() {
        pollingJob = lifecycleScope.launch(Dispatchers.IO) {
            while (isActive) {
                try {
                    val nowMs = SystemClock.elapsedRealtime()

                    val state = if (mode == "simulation") {
                        SimApi.api.getState()
                    } else {
                        // prototype → convert Arduino JSON → full StateResponse
                        val p = PrototypeApi.api.getState()
                        p.toStateResponse()
                    }

                    withContext(Dispatchers.Main) {

                        if (!initializedLayout || state.floorCount != floorCount) {
                            floorCount = state.floorCount
                            buildFloorLabels()
                            buildTicksAndResizeShafts()
                            initializedLayout = true
                        }

                        latestStates = state.elevators
                        updateTopLabels(latestStates)

                        latestStates.forEachIndexed { idx, e ->
                            if (idx >= tracks.size) return@forEachIndexed
                            updateTrackFromServer(idx, e, nowMs)
                        }
                    }
                } catch (_: Exception) {}

                delay(750)
            }
        }
    }

    // ---------- Same animation logic ----------
    private fun travelTimeMs(floorsMoved: Int): Long {
        val sec = if (floorsMoved <= 1) 7.5
        else 7.5 + 7.5 + 7.0 * (floorsMoved - 2)
        return (sec * 1000).toLong()
    }

    private fun updateTrackFromServer(i: Int, e: ElevatorState, nowMs: Long) {
        val t = tracks[i]

        when (e.state) {
            "Moving" -> {
                val floorsMoved = abs(e.targetFloor - e.currentFloor)
                val computedTotal = travelTimeMs(floorsMoved)

                val startingNewTrip = !t.isMoving || t.targetFloor != e.targetFloor

                if (startingNewTrip) {
                    t.isMoving = true
                    t.startFloor = e.currentFloor
                    t.targetFloor = e.targetFloor

                    t.totalTripMs = computedTotal
                    t.remainingMsReported = computedTotal
                    t.lastServerUpdateMs = nowMs
                } else {
                    t.lastServerUpdateMs = nowMs
                }
            }

            "DoorOpen", "Idle" -> {
                t.isMoving = false
                t.startFloor = e.currentFloor
                t.targetFloor = e.currentFloor
                elevators[i].translationY = floorToY(e.currentFloor)
            }

            else -> {
                t.isMoving = false
                elevators[i].translationY = floorToY(e.currentFloor)
            }
        }
    }

    private fun renderFrame() {
        if (!initializedLayout || latestStates.isEmpty()) return

        val nowMs = SystemClock.elapsedRealtime()

        latestStates.forEachIndexed { idx, e ->
            if (idx >= elevators.size) return@forEachIndexed
            val view = elevators[idx]
            val t = tracks[idx]

            if (t.isMoving && t.totalTripMs > 0) {
                val elapsed = nowMs - t.lastServerUpdateMs
                val remaining = (t.remainingMsReported - elapsed).coerceAtLeast(0)

                val progress = 1f - (remaining.toFloat() / t.totalTripMs.toFloat())
                    .coerceIn(0f, 1f)

                val startY = floorToY(t.startFloor)
                val targetY = floorToY(t.targetFloor)

                view.translationY = lerp(startY, targetY, progress)
            } else {
                view.translationY = floorToY(e.currentFloor)
            }
        }
    }

    // ---------- UI Rendering ----------
    private fun updateTopLabels(eStates: List<ElevatorState>) {
        val e1 = eStates.getOrNull(0)

        val modeText = if (mode == "simulation") "Simulation Mode" else "Prototype Mode"
        tvBuilding.text = "$modeText — $floorCount Floors"

        tvCurrentFloors.text = "Current Floor →  E1: ${e1?.currentFloor ?: "-"}"

        tvLoad.text = "Load: E1 ${e1?.load ?: 0}/${e1?.capacity ?: 10}"
    }

    private fun buildFloorLabels() {
        floorLabels.removeAllViews()

        for (f in floorCount downTo 1) {
            val label = TextView(this)
            label.text = "Floor $f"
            label.textSize = 16f
            label.gravity = Gravity.CENTER_VERTICAL
            label.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                dpToPx(floorHeightDp).toInt()
            )
            floorLabels.addView(label)
        }

        floorLabels.layoutParams.height = dpToPx(floorHeightDp * floorCount).toInt()
    }

    private fun buildTicksAndResizeShafts() {
        val shaftHeightPx = dpToPx(floorHeightDp * floorCount)

        shafts.forEachIndexed { idx, shaft ->

            if (mode == "prototype" && idx > 0) return@forEachIndexed

            shaft.removeAllViews()

            val lp = shaft.layoutParams
            lp.height = shaftHeightPx.toInt()
            shaft.layoutParams = lp

            for (i in 0 until floorCount) {
                val tick = View(this)
                tick.setBackgroundColor(Color.DKGRAY)
                val params = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    dpToPx(2f).toInt()
                )
                val y = shaftHeightPx - dpToPx(floorHeightDp) * (i + 1)
                tick.y = y
                shaft.addView(tick, params)
            }

            shaft.addView(elevators[idx])
        }
    }

    private fun floorToY(floor: Int): Float {
        val capped = max(1, min(floorCount, floor))
        return dpToPx(floorHeightDp * (floorCount - capped))
    }

    private fun lerp(a: Float, b: Float, t: Float): Float =
        a + (b - a) * t

    private fun dpToPx(dp: Float): Float =
        dp * resources.displayMetrics.density
}
