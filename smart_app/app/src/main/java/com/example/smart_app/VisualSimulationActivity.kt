package com.example.smart_app

import android.content.Intent
import android.graphics.Color
import android.os.*
import android.view.Gravity
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.ElevatorState
import com.example.smart_app.data.SimApi
import kotlinx.coroutines.*
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

    // ---------- Poll server slower; render loop handles smoothness ----------
    private fun startPolling() {
        pollingJob = lifecycleScope.launch(Dispatchers.IO) {
            while (isActive) {
                try {
                    val state = SimApi.api.getState()
                    val nowMs = SystemClock.elapsedRealtime()

                    withContext(Dispatchers.Main) {
                        if (!initializedLayout || state.floorCount != floorCount) {
                            floorCount = state.floorCount
                            buildFloorLabels()
                            buildTicksAndResizeShafts()
                            initializedLayout = true
                        }

                        latestStates = state.elevators
                        updateTopLabels(latestStates)

                        // Update movement tracks from server truth
                        latestStates.forEachIndexed { idx, e ->
                            if (idx >= tracks.size) return@forEachIndexed
                            updateTrackFromServer(idx, e, nowMs)
                        }
                    }
                } catch (_: Exception) {
                    // ignore transient errors
                }

                delay(750) // polling period; smoothness comes from render loop
            }
        }
    }

    private fun updateTrackFromServer(i: Int, e: ElevatorState, nowMs: Long) {
        val t = tracks[i]

        when (e.state) {
            "Moving" -> {
                // If newly entered moving or target changed, reset track
                val startingNewTrip = !t.isMoving || t.targetFloor != e.targetFloor

                if (startingNewTrip) {
                    t.isMoving = true
                    t.startFloor = e.currentFloor
                    t.targetFloor = e.targetFloor
                    t.totalTripMs = e.remainingMs
                    t.remainingMsReported = e.remainingMs
                    t.lastServerUpdateMs = nowMs
                } else {
                    // continuing same trip: just refresh remainingMs/time anchor
                    t.remainingMsReported = e.remainingMs
                    t.lastServerUpdateMs = nowMs

                    // If server says remaining grew (rare), treat as new trip
                    if (e.remainingMs > t.totalTripMs) {
                        t.totalTripMs = e.remainingMs
                    }
                }
            }

            "DoorOpen", "Idle" -> {
                // Not moving -> snap to floor and clear track
                t.isMoving = false
                t.startFloor = e.currentFloor
                t.targetFloor = e.currentFloor
                t.totalTripMs = 0
                t.remainingMsReported = 0
                t.lastServerUpdateMs = nowMs

                // ensure exact placement:
                val y = floorToY(e.currentFloor)
                elevators[i].translationY = y
            }
        }
    }

    // ---------- 60fps render ----------
    private fun renderFrame() {
        if (!initializedLayout || latestStates.isEmpty()) return

        val nowMs = SystemClock.elapsedRealtime()

        latestStates.forEachIndexed { idx, e ->
            if (idx >= elevators.size) return@forEachIndexed
            val view = elevators[idx]
            val t = tracks[idx]

            if (t.isMoving && t.totalTripMs > 0) {
                val elapsedSinceServer = nowMs - t.lastServerUpdateMs
                val estRemaining = (t.remainingMsReported - elapsedSinceServer).coerceAtLeast(0)

                val progress =
                    1f - (estRemaining.toFloat() / t.totalTripMs.toFloat()).coerceIn(0f, 1f)

                val startY = floorToY(t.startFloor)
                val targetY = floorToY(t.targetFloor)

                val y = lerp(startY, targetY, progress)
                view.translationY = y
            } else {
                // not moving: keep snapped
                view.translationY = floorToY(e.currentFloor)
            }
        }
    }

    // ---------- Layout builders ----------
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

    private fun updateTopLabels(eStates: List<ElevatorState>) {
        val e1 = eStates.getOrNull(0)
        val e2 = eStates.getOrNull(1)
        val e3 = eStates.getOrNull(2)

        tvBuilding.text = "Simulation Mode — $floorCount Floors"

        tvCurrentFloors.text =
            "Current Floors →  E1: ${e1?.currentFloor ?: "-"} | " +
                    "E2: ${e2?.currentFloor ?: "-"} | " +
                    "E3: ${e3?.currentFloor ?: "-"}"

        tvLoad.text =
            "Load: E1 ${e1?.load ?: 0}/${e1?.capacity ?: 10}  |  " +
                    "E2 ${e2?.load ?: 0}/${e2?.capacity ?: 10}  |  " +
                    "E3 ${e3?.load ?: 0}/${e3?.capacity ?: 10}"
    }

    // ---------- Helpers ----------
    private fun floorToY(floor: Int): Float {
        val capped = max(1, min(floorCount, floor))
        return dpToPx(floorHeightDp * (floorCount - capped))
    }

    private fun lerp(a: Float, b: Float, t: Float): Float =
        a + (b - a) * t

    private fun dpToPx(dp: Float): Float =
        dp * resources.displayMetrics.density
}
