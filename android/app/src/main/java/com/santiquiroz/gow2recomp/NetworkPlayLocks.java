// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.util.Log;

final class NetworkPlayLocks {
    private static final String TAG = "Gow2Activity";
    private static final String LOCK_TAG = "gow2-system-link";

    private final WifiManager wifiManager;
    private WifiManager.MulticastLock multicastLock;
    private WifiManager.WifiLock wifiLock;

    NetworkPlayLocks(Context context) {
        wifiManager = lookupWifiManager(context);
    }

    void acquire() {
        acquireMulticastLock();
        acquireWifiLock();
    }

    void release() {
        releaseMulticastLock();
        releaseWifiLock();
    }

    private static WifiManager lookupWifiManager(Context context) {
        if (context == null) {
            return null;
        }
        try {
            return context.getApplicationContext().getSystemService(WifiManager.class);
        } catch (RuntimeException e) {
            Log.w(TAG, "no Wi-Fi service; System Link plays without locks", e);
            return null;
        }
    }

    private void acquireMulticastLock() {
        if (wifiManager == null) {
            return;
        }
        try {
            if (multicastLock == null) {
                // Keeps the Wi-Fi driver from dropping LAN broadcasts (the System Link beacon) in power save.
                multicastLock = wifiManager.createMulticastLock(LOCK_TAG);
                multicastLock.setReferenceCounted(false);
            }
            if (!multicastLock.isHeld()) {
                multicastLock.acquire();
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot hold the multicast lock; LAN beacons may be missed", e);
        }
    }

    private void acquireWifiLock() {
        if (wifiManager == null) {
            return;
        }
        try {
            if (wifiLock == null) {
                // Keeps the radio out of power save during play.
                wifiLock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, LOCK_TAG);
                wifiLock.setReferenceCounted(false);
            }
            if (!wifiLock.isHeld()) {
                wifiLock.acquire();
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot hold the Wi-Fi lock; the radio may sleep during play", e);
        }
    }

    private void releaseMulticastLock() {
        try {
            if (multicastLock != null && multicastLock.isHeld()) {
                multicastLock.release();
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot release the multicast lock", e);
        }
    }

    private void releaseWifiLock() {
        try {
            if (wifiLock != null && wifiLock.isHeld()) {
                wifiLock.release();
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot release the Wi-Fi lock", e);
        }
    }
}
