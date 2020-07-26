//
// BLE garage door opener remote control
//
// Copyright (C) 2020, Stephan <kiffie@mailbox.org>
// SPDX-License-Identifier: GPL-2.0-or-later
//

package eu.stlck.garagedoor

import android.app.Dialog
import android.os.Bundle
import android.app.AlertDialog
import android.text.Editable
import android.text.TextWatcher
import android.widget.EditText
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import org.apache.commons.codec.binary.Base32
import java.lang.IllegalArgumentException
import java.util.zip.CRC32

class SetupDialogFragment(private val rxmkeyCallback: (ByteArray) -> Unit) : DialogFragment() {

    private var rxmkey: ByteArray = byteArrayOf()

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {

        val dialog = activity?.let {
            val builder = AlertDialog.Builder(it)
            val inflater = requireActivity().layoutInflater
            val v = inflater.inflate(R.layout.dialog_setup, null)
            builder.setView(v)
            builder.setMessage(R.string.setup_title)
            builder.apply {
                setPositiveButton(
                    R.string.ok) { _, _ ->
                    // User clicked OK button
                    if (rxmkey.isNotEmpty()) {
                        rxmkeyCallback(rxmkey)
                    }
                }
            }
            //val cs = findViewById<TextView>(R.id.setup_checksum)
            val cs = v.findViewById<TextView>(R.id.setup_checksum)
            val rxmk = v.findViewById<EditText>(R.id.setup_rxm_key)
            rxmk.addTextChangedListener(object: TextWatcher {
                override fun afterTextChanged(s: Editable) { }

                override fun beforeTextChanged(s: CharSequence, start: Int, count: Int, after: Int) { }
                override fun onTextChanged(s: CharSequence, start: Int, before: Int, count: Int) {
                    val base32 = Base32()
                    val base32Text = s.filter { ch -> base32.isInAlphabet(ch.toByte()) }
                    rxmkey = try {
                        base32.decode(base32Text.toString())
                    } catch (e: IllegalArgumentException) {
                        // use empty byte array in case of invalid imput base33 string
                        byteArrayOf()
                    }
                    val crc32 = CRC32()
                    crc32.update(rxmkey)
                    val message = "%d bytes, checksum: %08x".format(rxmkey.size, crc32.value)
                    cs.text = message
                }
            })
            builder.create()
        } ?: throw IllegalStateException("Activity cannot be null")

        dialog.setCancelable(false)
        dialog.setCanceledOnTouchOutside(false)
        return dialog

    }
}
