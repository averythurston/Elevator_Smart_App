package com.example.smart_app

import android.animation.ArgbEvaluator
import android.animation.ObjectAnimator
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity

class VisualSimulationActivity : AppCompatActivity() {

    private lateinit var elevator: TextView
    private lateinit var tvBuilding: TextView
    private lateinit var floorsContainer: LinearLayout
    private lateinit var tvCurrentFloor: TextView
    private lateinit var shaft: FrameLayout
    private val handler = Handler(Looper.getMainLooper())

    private val floorPositions = mutableListOf<Float>()
    private var currentFloorIndex = 0
    private var goingUp = true
    private val floorHeightDp = 80f   // Each floor = elevator height

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_visual_simulation)

        elevator = findViewById(R.id.elevator)
        tvBuilding = findViewById(R.id.tvBuilding)
        floorsContainer = findViewById(R.id.floorsContainer)
        tvCurrentFloor = findViewById(R.id.tvCurrentFloor)
        shaft = findViewById(R.id.shaftContainer)
        val btnStats = findViewById<Button>(R.id.btnStats)

        val buildingName = intent.getStringExtra("building") ?: "3 Floors"
        val userType = intent.getStringExtra("userType") ?: "Guest"
        val floorCount = when {
            buildingName.contains("5") -> 5
            buildingName.contains("4") -> 4
            else -> 3
        }

        tvBuilding.text = "Monitoring: $floorCount-floor building ($userType)"

        // --- Build floor labels dynamically, centered in each floor zone ---
        floorsContainer.removeAllViews()
        for (i in floorCount downTo 1) {
            val label = TextView(this)
            label.text = "Floor $i"
            label.textSize = 16f
            label.gravity = Gravity.CENTER
            label.setTextColor(Color.BLACK)
            label.setBackgroundColor(Color.TRANSPARENT)

            val params = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dpToPx(floorHeightDp).toInt() // each label 80dp tall
            )
            floorsContainer.addView(label, params)
        }

        // --- Build shaft and start elevator animation ---
        shaft.post {
            buildFloorTicks(floorCount)
            computeFloorPositions(floorCount)
            startElevatorCycle(floorCount)
        }

        btnStats.setOnClickListener {
            startActivity(Intent(this, StatisticsActivity::class.java))
        }
    }

    /** Builds horizontal tick marks at each floor position */
    private fun buildFloorTicks(floorCount: Int) {
        shaft.removeAllViews()
        val shaftHeight = floorCount * dpToPx(floorHeightDp)
        shaft.layoutParams.height = shaftHeight.toInt()

        // Add tick marks from bottom to top
        for (i in 0 until floorCount) {
            val tick = View(this)
            val params = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                dpToPx(2f).toInt()
            )
            tick.setBackgroundColor(Color.DKGRAY)
            tick.y = shaftHeight - dpToPx(floorHeightDp) * (i + 1)
            shaft.addView(tick, params)
        }

        // Add the elevator last so it appears above the lines
        shaft.addView(elevator)
    }

    /** Calculates translationY values for each floor stop */
    private fun computeFloorPositions(floorCount: Int) {
        floorPositions.clear()
        val totalHeight = (floorCount - 1) * dpToPx(floorHeightDp)
        for (i in 0 until floorCount) {
            val y = totalHeight - i * dpToPx(floorHeightDp)
            floorPositions.add(y)
        }
        currentFloorIndex = 0
        elevator.translationY = floorPositions.first()
        updateCurrentFloorLabel(currentFloorIndex + 1) // ✅ bottom index 0 -> "Floor 1"
    }

    private fun startElevatorCycle(floorCount: Int) {
        moveToFloor(currentFloorIndex, floorCount)
    }

    private fun moveToFloor(index: Int, floorCount: Int) {
        if (index !in floorPositions.indices) return
        val yPos = floorPositions[index]

        val anim = ObjectAnimator.ofFloat(elevator, "translationY", yPos)
        anim.duration = 1000
        anim.start()

        updateCurrentFloorLabel(index + 1) // ✅ index 0 -> "Floor 1", index 1 -> "Floor 2", etc.
        flashElevatorLight()

        handler.postDelayed({
            if (goingUp) {
                if (currentFloorIndex + 1 < floorPositions.size) currentFloorIndex++
                else { goingUp = false; currentFloorIndex-- }
            } else {
                if (currentFloorIndex - 1 >= 0) currentFloorIndex--
                else { goingUp = true; currentFloorIndex++ }
            }
            moveToFloor(currentFloorIndex, floorCount)
        }, 1500)
    }

    /** Simple visual flash at each stop */
    private fun flashElevatorLight() {
        val flash = ObjectAnimator.ofInt(
            elevator, "backgroundColor",
            Color.parseColor("#555555"),
            Color.parseColor("#AAAAAA"),
            Color.parseColor("#555555")
        )
        flash.duration = 700
        flash.setEvaluator(ArgbEvaluator())
        flash.start()
    }

    private fun updateCurrentFloorLabel(floor: Int) {
        tvCurrentFloor.text = "Current Floor: $floor"
    }

    /** Converts dp → px for consistent layout */
    private fun dpToPx(dp: Float): Float = dp * resources.displayMetrics.density

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacksAndMessages(null)
    }
}
