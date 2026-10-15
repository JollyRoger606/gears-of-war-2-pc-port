// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.util.Arrays;
import java.util.Collections;

import org.junit.Test;

public class LocalNetworkAddressTest {
    @Test
    public void wlan0WinsOverMobileAndHotspotEvenWhenListedLast() {
        assertEquals("192.168.10.114/24", LocalNetworkAddress.choose(Arrays.asList(
                up("rmnet_data0", "10.0.0.2", 29),
                up("swlan0", "192.168.43.1", 24),
                up("wlan0", "192.168.10.114", 24))));
    }

    @Test
    public void samsungHotspotAddressIsUsedWithoutClientWifi() {
        assertEquals("192.168.43.1/24", LocalNetworkAddress.choose(Arrays.asList(
                up("swlan0", "192.168.43.1", 24),
                up("rmnet_data1", "10.1.2.3", 30))));
    }

    @Test
    public void aospHotspotAddressIsUsedWithoutClientWifi() {
        assertEquals("192.168.43.1/24", LocalNetworkAddress.choose(Arrays.asList(
                up("ap0", "192.168.43.1", 24),
                up("rmnet_data1", "10.1.2.3", 30))));
    }

    @Test
    public void mobileDataOnlyFindsNothing() {
        assertNull(LocalNetworkAddress.choose(
                Collections.singletonList(up("rmnet_data0", "10.0.0.2", 29))));
    }

    @Test
    public void downWifiNeverWins() {
        assertEquals("192.168.1.6/24", LocalNetworkAddress.choose(Arrays.asList(
                down("wlan0", "192.168.1.5", 24),
                up("eth0", "192.168.1.6", 24))));
    }

    @Test
    public void loopbackNeverWins() {
        assertEquals("192.168.1.6/24", LocalNetworkAddress.choose(Arrays.asList(
                loopback("lo", "127.0.0.1", 8),
                up("eth0", "192.168.1.6", 24))));
    }

    @Test
    public void zeroPrefixIsSkipped() {
        assertEquals("192.168.1.6/24", LocalNetworkAddress.choose(Arrays.asList(
                up("wlan0", "192.168.1.5", 0),
                up("eth0", "192.168.1.6", 24))));
    }

    @Test
    public void extraWlanBeatsEthernet() {
        assertEquals("192.168.1.5/24", LocalNetworkAddress.choose(Arrays.asList(
                up("eth0", "192.168.1.6", 24),
                up("wlan1", "192.168.1.5", 24))));
    }

    @Test
    public void ethernetAloneStillWorks() {
        assertEquals("192.168.1.6/24", LocalNetworkAddress.choose(
                Collections.singletonList(up("eth0", "192.168.1.6", 24))));
    }

    @Test
    public void emptyListFindsNothing() {
        assertNull(LocalNetworkAddress.choose(Collections.<LocalNetworkAddress.Candidate>emptyList()));
    }

    @Test
    public void rankTiesKeepListOrder() {
        assertEquals("192.168.1.5/24", LocalNetworkAddress.choose(Arrays.asList(
                up("wlan1", "192.168.1.5", 24),
                up("wlan2", "192.168.1.7", 24))));
    }

    @Test
    public void unknownInterfaceNamesNeverQualify() {
        assertNull(LocalNetworkAddress.choose(Arrays.asList(
                up("tun0", "10.8.0.2", 24),
                up("p2p0", "192.168.49.1", 24),
                up("dummy0", "192.168.99.1", 24),
                up("ccmni0", "10.20.30.40", 29))));
    }

    private static LocalNetworkAddress.Candidate up(String interfaceName, String address, int prefixLength) {
        return new LocalNetworkAddress.Candidate(interfaceName, address, prefixLength, true, false);
    }

    private static LocalNetworkAddress.Candidate down(String interfaceName, String address, int prefixLength) {
        return new LocalNetworkAddress.Candidate(interfaceName, address, prefixLength, false, false);
    }

    private static LocalNetworkAddress.Candidate loopback(String interfaceName, String address, int prefixLength) {
        return new LocalNetworkAddress.Candidate(interfaceName, address, prefixLength, true, true);
    }
}
