//
// BLE garage door opener remote control
//
// Copyright (C) 2020, Stephan <kiffie@mailbox.org>
// SPDX-License-Identifier: GPL-2.0-or-later
//

package eu.stlck.garagedoor

import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.content.Intent
import android.os.Bundle
import com.google.android.material.snackbar.Snackbar
import androidx.appcompat.app.AppCompatActivity
import android.util.Log

import kotlinx.android.synthetic.main.activity_main.*
import kotlinx.android.synthetic.main.content_main.*
import java.util.*
import javax.crypto.spec.SecretKeySpec
import android.os.ParcelUuid
import java.lang.Exception
import java.nio.ByteBuffer
import javax.crypto.Mac
import org.apache.commons.codec.binary.Base64

const val TAG = "GD_MAIN"

class Identity(val uuid: UUID, val key: ByteArray)

class MainActivity : AppCompatActivity() {

    private val bluetoothAdapter: BluetoothAdapter? by lazy(LazyThreadSafetyMode.NONE) {
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }

    private var id: Identity? = null

    class MissingIdentityException : Exception()

    private fun loadIdentity(): Identity {
        // get or create sequence number
        val sharedPref = getPreferences(Context.MODE_PRIVATE)


        val uuidString = sharedPref.getString("id_uuid", null) ?:throw MissingIdentityException()
        val keyString = sharedPref.getString("id_key", null) ?: throw MissingIdentityException()
        val key: ByteArray
        try {
            key = Base64.decodeBase64(keyString)
        } catch (e: Exception) {
            throw MissingIdentityException()
        }
        val uuid = UUID.fromString(uuidString)
        return Identity(uuid, key)
    }

    private fun saveIdentity(id: Identity) {
        val sharedPref = getPreferences(Context.MODE_PRIVATE)
        with(sharedPref.edit()) {
            putString("id_uuid", id.uuid.toString())
            putString("id_key", Base64.encodeBase64String(id.key))
            commit()
        }



    }

    private fun calculateTxKey(uuid: UUID, rxm_key: ByteArray) : ByteArray {
        val keyspec = SecretKeySpec(rxm_key, "HmacSHA256")
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(keyspec)
        val buf = ByteBuffer.wrap(ByteArray(16))
        buf.putLong(uuid.mostSignificantBits)
        buf.putLong(uuid.leastSignificantBits)
        return mac.doFinal(buf.array())
    }

    private fun doSetupDialog() {
        val sud = SetupDialogFragment {
            // Generate new ID
            val uuid = UUID.randomUUID()
            id = Identity(uuid, calculateTxKey(uuid, it))
            saveIdentity(id!!)
        }
        sud.show(supportFragmentManager, "SetupDialogFragment")
    }

    fun bleAdv(id: Identity) {
        if (!bluetoothAdapter!!.isEnabled) {
            val btenaEvent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            startActivityForResult(btenaEvent, 0)
        }
        val advertiser = bluetoothAdapter?.bluetoothLeAdvertiser

        // get or create sequence number
        val sharedPref = getPreferences(Context.MODE_PRIVATE)
        val seqNo = sharedPref.getLong("adv_seq_no", 0)

        // HMAC calculation
        val keyspec = SecretKeySpec(id.key, "HmacSHA256")
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(keyspec)
        val counter = byteArrayOf(
            (seqNo shl 24).toByte(),
            (seqNo shl 16).toByte(),
            (seqNo shl  8).toByte(),
            seqNo.toByte())
        val digest = mac.doFinal(counter)

        // send Advertisement
        val advSettings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setTimeout(3000)
            .setConnectable(false)
            .build()
        val advcb = object : AdvertiseCallback() {
            override  fun onStartFailure(errorCode: Int) {
                Log.e(TAG, "could not start advertising: $errorCode")
            }

            override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) {
                Log.e(TAG, "advertising started")
            }
        }

        val message = counter + digest.sliceArray(0..3)
        Log.d(TAG, "message = %s".format(message.joinToString(
            separator = " ",
            transform = { b: Byte -> "%02x".format(b.toInt().and(0xff)) })))


        val data = AdvertiseData.Builder()
            .addServiceData(
                ParcelUuid(id.uuid),
                message)
            .build()
        advertiser?.startAdvertising(advSettings, data, advcb)

        // increment sequence number. The case where seq_no becomes > 0xffff_ffff is not handled
        with(sharedPref.edit()) {
            putLong("adv_seq_no", seqNo + 1)
            commit()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        setSupportActionBar(toolbar)

        try {
            id = loadIdentity()
            Log.d(TAG, "Identity UUID: " + id?.uuid.toString())
        } catch (e: MissingIdentityException) {
            doSetupDialog()
        }

        this.adv_button.setOnClickListener { view ->

            if (id != null) {
                Snackbar.make(view, "Advertising (%s)".format(id!!.uuid), Snackbar.LENGTH_LONG)
                    .setAction("Action", null).show()
                this.bleAdv(id!!)
            } else {
                doSetupDialog()
            }
        }
        Log.d(TAG, "Main Acitivity created")
    }

//    override fun onCreateOptionsMenu(menu: Menu): Boolean {
//        // Inflate the menu; this adds items to the action bar if it is present.
//        menuInflater.inflate(R.menu.menu_main, menu)
//        return true
//    }
//
//    override fun onOptionsItemSelected(item: MenuItem): Boolean {
//        // Handle action bar item clicks here. The action bar will
//        // automatically handle clicks on the Home/Up button, so long
//        // as you specify a parent activity in AndroidManifest.xml.
//        return when(item.itemId) {
//            R.id.action_settings -> true
//            else -> super.onOptionsItemSelected(item)
//        }
//    }
}
