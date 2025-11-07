package com.example.smart_app

import android.content.Intent
import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity

class LoginActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        val etUsername = findViewById<EditText>(R.id.etUsername)
        val etPassword = findViewById<EditText>(R.id.etPassword)
        val spinnerBuilding = findViewById<Spinner>(R.id.spinnerBuilding)
        val btnLogin = findViewById<Button>(R.id.btnLogin)
        val btnGuest = findViewById<Button>(R.id.btnGuest)

        val buildings = listOf("3 Floors", "4 Floors", "5 Floors")
        spinnerBuilding.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, buildings)

        btnLogin.setOnClickListener {
            if (etUsername.text.toString() == "admin" && etPassword.text.toString() == "1234") {
                val intent = Intent(this, VisualSimulationActivity::class.java)
                intent.putExtra("building", spinnerBuilding.selectedItem.toString())
                intent.putExtra("userType", "Admin")
                startActivity(intent)
            } else {
                Toast.makeText(this, "Invalid credentials", Toast.LENGTH_SHORT).show()
            }
        }

        btnGuest.setOnClickListener {
            val intent = Intent(this, VisualSimulationActivity::class.java)
            intent.putExtra("building", spinnerBuilding.selectedItem.toString())
            intent.putExtra("userType", "Guest")
            startActivity(intent)
        }
    }
}

