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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        val btnSim = findViewById<Button>(R.id.btnSimulation)
        val tvStatus = findViewById<TextView>(R.id.tvStatus)

        btnSim.setOnClickListener {
            tvStatus.text = "Connecting to simulation..."

            lifecycleScope.launch {
                try {
                    val state = withContext(Dispatchers.IO) { SimApi.api.getState() }
                    val floors = state.floorCount

                    tvStatus.text = "Connected — $floors floors detected."

                    val i = Intent(this@LoginActivity, VisualSimulationActivity::class.java)
                    i.putExtra("mode", "simulation")
                    i.putExtra("floorCount", floors)
                    startActivity(i)
                } catch (e: Exception) {
                    tvStatus.text = "Simulation server unreachable."
                }
            }
        }
    }
}