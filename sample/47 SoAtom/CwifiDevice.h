#pragma once
#include <circle/sdhci.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <circle/memio.h>

class CWiFiDevice
{
public:
    CWiFiDevice(CLogger *pLogger);
    boolean Initialize();
    boolean LoadFirmware();
    boolean StartCore();
    boolean Scan();
    boolean ConnectWPA2(const char *ssid, const char *passphrase);

    boolean DHCPRequest();
    boolean UDPSend(const char *ip, uint16_t port, const void *data, size_t len);

private:
    boolean SDIOWrite(uint32_t addr, const void *buf, size_t len);
    boolean SDIORead(uint32_t addr, void *buf, size_t len);

    boolean SendHCICommand(uint16_t opcode, const void *data, size_t len);
    boolean WaitForHCIEvent(uint16_t opcode);

    boolean WPA2Handshake(const uint8_t *pmk);

private:
    CLogger *m_pLogger;
    CSDHCI m_SDIO;

    uint8_t m_MAC[6];
    uint32_t m_IP;
};
