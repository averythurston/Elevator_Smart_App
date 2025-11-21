package com.example.smart_app

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.example.smart_app.data.SimApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class LoginActivity : AppCompatActivity() {

    private val api by lazy {
        SimApi.create("http://10.0.2.2:8080/")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        val btnSimulation = findViewById<Button>(R.id.btnSimulation)
        val btnPrototype = findViewById<Button>(R.id.btnPrototype)
        val tvStatus = findViewById<TextView>(R.id.tvStatus)

        btnSimulation.setOnClickListener {
            tvStatus.text = "Connecting to simulation server..."

            lifecycleScope.launch {
                try {
                    val state = withContext(Dispatchers.IO) { api.getState() }
                    val floorCount = state.floorCount

                    tvStatus.text = "Connected — $floorCount floors detected."

                    val intent = Intent(this@LoginActivity, VisualSimulationActivity::class.java)
                    intent.putExtra("mode", "simulation")
                    intent.putExtra("floorCount", floorCount)
                    startActivity(intent)

                } catch (e: Exception) {
                    tvStatus.text = "Simulation server unreachable."
                }
            }
        }

        btnPrototype.setOnClickListener {
            val intent = Intent(this, VisualSimulationActivity::class.java)
            intent.putExtra("mode", "prototype")
            intent.putExtra("floorCount", 5) // placeholder for future prototype logic
            startActivity(intent)
        }
    }
}

