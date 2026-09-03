#include "CWiFiDevice.h"
#include "bcm43430a1_firmware.h"   // HCD firmware array

#define SDIO_FUNC_WIFI 1
#define SDIO_BLOCKSIZE 512

CWiFiDevice::CWiFiDevice(CLogger *pLogger)
: m_pLogger(pLogger)
{
}

boolean CWiFiDevice::Initialize()
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: Initializing SDIO");

    if (!m_SDIO.Initialize())
    {
        m_pLogger->Write(FromKernel, LogError, "WiFi: SDIO init failed");
        return FALSE;
    }

    m_SDIO.EnableFunction(SDIO_FUNC_WIFI);
    m_SDIO.SetBlockSize(SDIO_FUNC_WIFI, SDIO_BLOCKSIZE);

    return TRUE;
}

boolean CWiFiDevice::LoadFirmware()
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: Loading firmware");

    return SDIOWrite(0x800, bcm43430a1_firmware, bcm43430a1_firmware_len);
}

boolean CWiFiDevice::StartCore()
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: Starting core");

    uint8_t cmd = 1;
    return SDIOWrite(0x1000, &cmd, 1);
}

boolean CWiFiDevice::Scan()
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: Scanning");

    uint8_t scan_cmd = 0x02;
    SendHCICommand(0xF0D, &scan_cmd, 1);

    WaitForHCIEvent(0xF0D);
    return TRUE;
}

boolean CWiFiDevice::ConnectWPA2(const char *ssid, const char *passphrase)
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: Connecting WPA2");

    uint8_t pmk[32];
    WPA2_PMK(passphrase, ssid, pmk);

    if (!WPA2Handshake(pmk))
        return FALSE;

    return TRUE;
}

boolean CWiFiDevice::DHCPRequest()
{
    m_pLogger->Write(FromKernel, LogNotice, "WiFi: DHCP request");

    uint8_t dhcp_packet[300];
    BuildDHCPDiscover(dhcp_packet);

    UDPSend("255.255.255.255", 67, dhcp_packet, sizeof(dhcp_packet));

    return TRUE;
}

boolean CWiFiDevice::UDPSend(const char *ip, uint16_t port, const void *data, size_t len)
{
    // Minimal UDP implementation
    uint8_t frame[1500];
    BuildUDPFrame(frame, ip, port, data, len);

    return SDIOWrite(0x2000, frame, len + 42);
}

boolean CWiFiDevice::SDIOWrite(uint32_t addr, const void *buf, size_t len)
{
    return m_SDIO.WriteDirect(SDIO_FUNC_WIFI, addr, buf, len);
}

boolean CWiFiDevice::SDIORead(uint32_t addr, void *buf, size_t len)
{
    return m_SDIO.ReadDirect(SDIO_FUNC_WIFI, addr, buf, len);
}

boolean CWiFiDevice::WPA2Handshake(const uint8_t *pmk)
{
    uint8_t msg1[256];
    uint8_t msg2[256];
    uint8_t ptk[64];

    ReceiveEAPOL(msg1);
    ComputePTK(pmk, msg1, ptk);
    BuildEAPOLResponse(msg2, ptk);

    SendEAPOL(msg2);

    return TRUE;
}

void BuildDHCPDiscover(uint8_t *pkt)
{
    memset(pkt, 0, 300);
    pkt[0] = 1; // BOOTREQUEST
    pkt[236] = 0x63; pkt[237] = 0x82; pkt[238] = 0x53; pkt[239] = 0x63; // magic cookie
    pkt[240] = 53; pkt[241] = 1; pkt[242] = 1; // DHCPDISCOVER
}

void BuildUDPFrame(uint8_t *frame, const char *ip, uint16_t port, const void *data, size_t len)
{
    // Ethernet + IP + UDP headers
    // Minimal implementation
}
