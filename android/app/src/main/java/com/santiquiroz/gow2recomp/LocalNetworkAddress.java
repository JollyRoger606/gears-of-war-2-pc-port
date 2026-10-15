// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.util.Log;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;

final class LocalNetworkAddress {
    private static final String TAG = "Gow2Activity";
    private static final int MIN_PREFIX_LENGTH = 1;
    private static final int MAX_PREFIX_LENGTH = 32;
    private static final int RANK_WLAN0 = 0;
    private static final int RANK_WLAN = 1;
    private static final int RANK_HOTSPOT = 2;
    private static final int RANK_ETH = 3;
    private static final int NO_RANK = -1;
    private static final String WIFI_CLIENT_NAME = "wlan0";
    private static final String WIFI_NAME_PREFIX = "wlan";
    private static final String SAMSUNG_HOTSPOT_PREFIX = "swlan";
    private static final String AOSP_HOTSPOT_PREFIX = "ap";
    private static final String ETHERNET_PREFIX = "eth";

    private LocalNetworkAddress() {
    }

    static final class Candidate {
        final String interfaceName;
        final String address;
        final int prefixLength;
        final boolean up;
        final boolean loopback;

        Candidate(String interfaceName, String address, int prefixLength, boolean up, boolean loopback) {
            this.interfaceName = interfaceName;
            this.address = address;
            this.prefixLength = prefixLength;
            this.up = up;
            this.loopback = loopback;
        }
    }

    static String choose(List<Candidate> candidates) {
        if (candidates == null) {
            return null;
        }
        String best = null;
        int bestRank = Integer.MAX_VALUE;
        for (Candidate candidate : candidates) {
            if (!isSelectable(candidate)) {
                continue;
            }
            int rank = rankByInterfaceName(candidate.interfaceName);
            // Strictly lower wins so ties keep list order.
            if (rank < bestRank) {
                bestRank = rank;
                best = candidate.address + "/" + candidate.prefixLength;
            }
        }
        return best;
    }

    static String detect() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            if (interfaces == null) {
                Log.w(TAG, "no network interfaces found; System Link uses the runtime's default");
                return null;
            }
            return choose(collectCandidates(interfaces));
        } catch (SocketException | RuntimeException e) {
            Log.w(TAG, "cannot list network interfaces; System Link uses the runtime's default", e);
            return null;
        }
    }

    private static boolean isSelectable(Candidate candidate) {
        if (candidate == null || !candidate.up || candidate.loopback) {
            return false;
        }
        if (candidate.address == null) {
            return false;
        }
        if (candidate.prefixLength < MIN_PREFIX_LENGTH || candidate.prefixLength > MAX_PREFIX_LENGTH) {
            return false;
        }
        return rankByInterfaceName(candidate.interfaceName) != NO_RANK;
    }

    private static int rankByInterfaceName(String interfaceName) {
        if (interfaceName == null) {
            return NO_RANK;
        }
        if (interfaceName.equals(WIFI_CLIENT_NAME)) {
            return RANK_WLAN0;
        }
        if (interfaceName.startsWith(WIFI_NAME_PREFIX)) {
            return RANK_WLAN;
        }
        if (interfaceName.startsWith(SAMSUNG_HOTSPOT_PREFIX) || interfaceName.startsWith(AOSP_HOTSPOT_PREFIX)) {
            return RANK_HOTSPOT;
        }
        if (interfaceName.startsWith(ETHERNET_PREFIX)) {
            return RANK_ETH;
        }
        return NO_RANK;
    }

    private static List<Candidate> collectCandidates(Enumeration<NetworkInterface> interfaces) throws SocketException {
        List<Candidate> candidates = new ArrayList<>();
        while (interfaces.hasMoreElements()) {
            collectInterfaceCandidates(interfaces.nextElement(), candidates);
        }
        return candidates;
    }

    private static void collectInterfaceCandidates(NetworkInterface networkInterface, List<Candidate> candidates)
            throws SocketException {
        boolean up = networkInterface.isUp();
        boolean loopback = networkInterface.isLoopback();
        String name = networkInterface.getName();
        for (InterfaceAddress interfaceAddress : networkInterface.getInterfaceAddresses()) {
            InetAddress address = interfaceAddress.getAddress();
            if (!(address instanceof Inet4Address)) {
                continue;
            }
            candidates.add(new Candidate(name, address.getHostAddress(),
                    interfaceAddress.getNetworkPrefixLength(), up, loopback));
        }
    }
}
