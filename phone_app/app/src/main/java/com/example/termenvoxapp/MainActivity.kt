package com.example.termenvoxapp

import android.Manifest
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.widget.addTextChangedListener
import java.util.UUID

class MainActivity : AppCompatActivity() {

    // UI
    private lateinit var statusTextView: TextView
    private lateinit var connectButton: Button
    private lateinit var min1EditText: EditText
    private lateinit var max1EditText: EditText
    private lateinit var min2EditText: EditText
    private lateinit var max2EditText: EditText
    private lateinit var seekBar1: SeekBar
    private lateinit var seekBar2: SeekBar
    private lateinit var value1TextView: TextView
    private lateinit var value2TextView: TextView

    // Громкость (третий слайдер)
    private lateinit var volumeSeekBar: SeekBar
    private lateinit var volumeValueTextView: TextView

    private lateinit var soundCheckBox: CheckBox
    private lateinit var mouseCheckBox: CheckBox

    // BLE
    private lateinit var bluetoothAdapter: BluetoothAdapter
    private var bluetoothGatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null

    private var isConnected = false

    companion object {
        private const val DEVICE_NAME = "ESP32_LED"

        private val SERVICE_UUID: UUID =
            UUID.fromString("12345678-1234-5678-1234-56789abcdef0")
        private val CHARACTERISTIC_UUID_TX: UUID =
            UUID.fromString("12345678-1234-5678-1234-56789abcdef1")

        private const val PERMISSION_REQUEST = 1001
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // UI
        statusTextView = findViewById(R.id.statusTextView)
        connectButton = findViewById(R.id.connectButton)

        min1EditText = findViewById(R.id.min1EditText)
        max1EditText = findViewById(R.id.max1EditText)
        min2EditText = findViewById(R.id.min2EditText)
        max2EditText = findViewById(R.id.max2EditText)

        seekBar1 = findViewById(R.id.seekBar1)
        seekBar2 = findViewById(R.id.seekBar2)

        value1TextView = findViewById(R.id.value1TextView)
        value2TextView = findViewById(R.id.value2TextView)

        // Громкость
        volumeSeekBar = findViewById(R.id.volumeSeekBar)
        volumeValueTextView = findViewById(R.id.volumeValueTextView)

        // тумблеры
        soundCheckBox = findViewById(R.id.soundCheckBox)
        mouseCheckBox = findViewById(R.id.mouseCheckBox)


        // значения по умолчанию
        min1EditText.setText("0")
        max1EditText.setText("4095")
        min2EditText.setText("0")
        max2EditText.setText("4095")

        // громкость по умолчанию 0..100
        volumeSeekBar.max = 100
        volumeSeekBar.progress = 50

        // Bluetooth
        val manager = getSystemService(BluetoothManager::class.java)
        bluetoothAdapter = manager.adapter

        connectButton.setOnClickListener {
            if (!isConnected) {
                checkPermissions()
            } else {
                disconnect()
            }
        }

        // Слайдеры — слушатели
        seekBar1.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                updateCurrentValuesText()
                sendValues()
            }

            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        seekBar2.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                updateCurrentValuesText()
                sendValues()
            }

            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        // Громкость — третий слайдер, логика такая же:
        volumeSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                updateVolumeText()
                sendValues()
            }

            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        soundCheckBox.setOnCheckedChangeListener { _, _ ->
            sendValues()
        }

        mouseCheckBox.setOnCheckedChangeListener { _, _ ->
            sendValues()
        }

        // При изменении MIN/MAX — обновляем диапазоны слайдеров
        min1EditText.addTextChangedListener { updateSeekBarRanges() }
        max1EditText.addTextChangedListener { updateSeekBarRanges() }
        min2EditText.addTextChangedListener { updateSeekBarRanges() }
        max2EditText.addTextChangedListener { updateSeekBarRanges() }

        // Инициализируем диапазоны и текст
        updateSeekBarRanges()
        updateCurrentValuesText()
        updateVolumeText()
    }

    // ---------------------------------------------------------
    // Вспомогательные функции для чисел / диапазонов
    // ---------------------------------------------------------

    private fun parseIntOrDefault(text: String?, default: Int): Int {
        return text?.toIntOrNull() ?: default
    }

    /** Обновляет max у слайдеров так, чтобы они ходили от MIN до MAX */
    private fun updateSeekBarRanges() {
        val min1 = parseIntOrDefault(min1EditText.text?.toString(), 0)
        val max1 = parseIntOrDefault(max1EditText.text?.toString(), 4095)
        val min2 = parseIntOrDefault(min2EditText.text?.toString(), 0)
        val max2 = parseIntOrDefault(max2EditText.text?.toString(), 4095)

        val range1 = (max1 - min1).coerceAtLeast(0)
        val range2 = (max2 - min2).coerceAtLeast(0)

        seekBar1.max = if (range1 == 0) 0 else range1
        seekBar2.max = if (range2 == 0) 0 else range2

        // Если текущий progress вышел за новый диапазон — обрежем
        if (seekBar1.progress > seekBar1.max) seekBar1.progress = seekBar1.max
        if (seekBar2.progress > seekBar2.max) seekBar2.progress = seekBar2.max

        updateCurrentValuesText()
    }

    /** Реальные значения, которые должны уйти на плату: MIN + progress */
    private fun calculateCurrentValues(): Pair<Int, Int> {
        val min1 = parseIntOrDefault(min1EditText.text?.toString(), 0)
        val max1 = parseIntOrDefault(max1EditText.text?.toString(), 4095)
        val min2 = parseIntOrDefault(min2EditText.text?.toString(), 0)
        val max2 = parseIntOrDefault(max2EditText.text?.toString(), 4095)

        val raw1 = min1 + seekBar1.progress
        val raw2 = min2 + seekBar2.progress

        val value1 = raw1.coerceIn(min1, max1)
        val value2 = raw2.coerceIn(min2, max2)

        return value1 to value2
    }

    /** Текущее значение громкости 0..100 */
    private fun calculateVolumeValue(): Int {
        return volumeSeekBar.progress.coerceIn(0, 100)
    }

    private fun updateCurrentValuesText() {
        val (v1, v2) = calculateCurrentValues()
        value1TextView.text = "Значение 1: $v1"
        value2TextView.text = "Значение 2: $v2"
    }

    private fun updateVolumeText() {
        val v = calculateVolumeValue()
        volumeValueTextView.text = v.toString()
    }

    // ---------------------------------------------------------
    // Разрешения
    // ---------------------------------------------------------

    private fun checkPermissions() {
        val needed = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            needed += Manifest.permission.BLUETOOTH_SCAN
            needed += Manifest.permission.BLUETOOTH_CONNECT
        } else {
            needed += Manifest.permission.ACCESS_COARSE_LOCATION
        }

        val notGranted = needed.filter {
            ActivityCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (notGranted.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, notGranted.toTypedArray(), PERMISSION_REQUEST)
        } else {
            startScan()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)

        if (requestCode == PERMISSION_REQUEST) {
            if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                startScan()
            } else {
                statusTextView.text = "Разрешения не выданы"
            }
        }
    }

    // ---------------------------------------------------------
    // Сканирование BLE
    // ---------------------------------------------------------

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(type: Int, result: ScanResult) {

            val permissionForConnect =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                    Manifest.permission.BLUETOOTH_CONNECT
                else
                    Manifest.permission.BLUETOOTH

            if (ActivityCompat.checkSelfPermission(this@MainActivity, permissionForConnect)
                != PackageManager.PERMISSION_GRANTED
            ) {
                return
            }

            val device = result.device ?: return
            val name = device.name

            if (name == DEVICE_NAME) {
                stopScan()
                connectTo(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            runOnUiThread {
                statusTextView.text = "Сканирование не удалось: $errorCode"
            }
        }
    }

    private fun startScan() {
        statusTextView.text = "Сканирование..."

        val permissionForScan =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_SCAN
            else
                Manifest.permission.ACCESS_COARSE_LOCATION

        if (ActivityCompat.checkSelfPermission(this, permissionForScan)
            != PackageManager.PERMISSION_GRANTED
        ) {
            statusTextView.text = "Нет разрешения на сканирование"
            return
        }

        bluetoothAdapter.bluetoothLeScanner.startScan(scanCallback)

        Handler(Looper.getMainLooper()).postDelayed({
            stopScan()
        }, 8000)
    }

    private fun stopScan() {
        val permissionForScan =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_SCAN
            else
                Manifest.permission.ACCESS_COARSE_LOCATION

        if (ActivityCompat.checkSelfPermission(this, permissionForScan)
            != PackageManager.PERMISSION_GRANTED
        ) {
            return
        }

        try {
            bluetoothAdapter.bluetoothLeScanner.stopScan(scanCallback)
        } catch (_: Exception) {
        }
    }

    // ---------------------------------------------------------
    // Подключение BLE
    // ---------------------------------------------------------

    private fun connectTo(device: BluetoothDevice) {
        statusTextView.text = "Подключение..."

        val permissionForConnect =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_CONNECT
            else
                Manifest.permission.BLUETOOTH

        if (ActivityCompat.checkSelfPermission(this, permissionForConnect)
            != PackageManager.PERMISSION_GRANTED
        ) {
            statusTextView.text = "Нет разрешения на подключение"
            return
        }

        bluetoothGatt = device.connectGatt(this, false, gattCallback)
    }

    private fun disconnect() {
        val permissionForConnect =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_CONNECT
            else
                Manifest.permission.BLUETOOTH

        if (ActivityCompat.checkSelfPermission(this, permissionForConnect)
            == PackageManager.PERMISSION_GRANTED
        ) {
            bluetoothGatt?.close()
        }

        bluetoothGatt = null
        txCharacteristic = null

        isConnected = false
        connectButton.text = "Подключиться"
        statusTextView.text = "Отключено"
    }

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {

                val permissionForConnect =
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                        Manifest.permission.BLUETOOTH_CONNECT
                    else
                        Manifest.permission.BLUETOOTH

                if (ActivityCompat.checkSelfPermission(this@MainActivity, permissionForConnect)
                    != PackageManager.PERMISSION_GRANTED
                ) {
                    runOnUiThread {
                        statusTextView.text = "Нет разрешения на discoverServices"
                    }
                    return
                }

                gatt.discoverServices()
                runOnUiThread { statusTextView.text = "Поиск сервисов..." }

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                runOnUiThread { disconnect() }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {

                val service = gatt.getService(SERVICE_UUID)
                val ch = service?.getCharacteristic(CHARACTERISTIC_UUID_TX)

                if (ch != null) {
                    txCharacteristic = ch
                    runOnUiThread {
                        isConnected = true
                        connectButton.text = "Отключиться"
                        statusTextView.text = "Готово, можно двигать слайдеры"
                        updateSeekBarRanges()
                        updateCurrentValuesText()
                        updateVolumeText()
                        sendValues()
                    }
                } else {
                    runOnUiThread {
                        statusTextView.text = "Сервис/характеристика не найдены"
                    }
                }
            } else {
                runOnUiThread {
                    statusTextView.text = "Ошибка discoverServices: $status"
                }
            }
        }
    }

    // ---------------------------------------------------------
    // Отправка трёх значений
    // ---------------------------------------------------------

    private fun sendValues() {
        if (!isConnected) return

        val gatt = bluetoothGatt ?: return
        val ch = txCharacteristic ?: return

        val permissionForConnect =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_CONNECT
            else
                Manifest.permission.BLUETOOTH

        if (ActivityCompat.checkSelfPermission(this, permissionForConnect)
            != PackageManager.PERMISSION_GRANTED
        ) {
            statusTextView.text = "Нет разрешения для записи в BLE"
            return
        }

        val (v1, v2) = calculateCurrentValues()
        val v3 = calculateVolumeValue()

        val soundFlag = if (soundCheckBox.isChecked) 1 else 0
        val mouseFlag = if (mouseCheckBox.isChecked) 1 else 0

        // протокол: A, B, C, S, M
        val msg = "A:$v1;B:$v2;C:$v3;S:$soundFlag;M:$mouseFlag\n"
        val data = msg.toByteArray(Charsets.UTF_8)

        ch.value = data
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        gatt.writeCharacteristic(ch)
    }


    // ---------------------------------------------------------
    // Очистка
    // ---------------------------------------------------------

    override fun onDestroy() {
        super.onDestroy()

        val permissionForConnect =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                Manifest.permission.BLUETOOTH_CONNECT
            else
                Manifest.permission.BLUETOOTH

        if (ActivityCompat.checkSelfPermission(this, permissionForConnect)
            == PackageManager.PERMISSION_GRANTED
        ) {
            bluetoothGatt?.close()
        }

        bluetoothGatt = null
        txCharacteristic = null
    }
}
