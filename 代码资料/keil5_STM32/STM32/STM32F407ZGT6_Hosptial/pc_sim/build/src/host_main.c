#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hmi_driver.h"
#include "cmd_process.h"
#include "cmd_queue.h"
#include "app_spo2.h"

#if CRC16_ENABLE
#error This PC adapter currently requires CRC16_ENABLE == 0.
#endif

typedef char require_uint32_is_32_bits[(sizeof(uint32) == 4) ? 1 : -1];

volatile uint32 timer_tick_count = 0;
uint8 cmd_buffer[sizeof(CTRL_MSG)];

static HANDLE serial_handle = INVALID_HANDLE_VALUE;
static const char *port_name = "COM8";
static volatile LONG stop_requested = 0;
static LARGE_INTEGER frequency, epoch;
static unsigned char tx_buffer[4096];
static DWORD tx_used;
static unsigned long tx_total, rx_total;
static unsigned char rx_frame[CMD_MAX_SIZE];
static unsigned int rx_used;
static uint32 rx_tail;
static ULONGLONG rx_last_ms;

static void fail(const char *operation)
{
    DWORD code = GetLastError();
    fprintf(stderr, "%s failed on %s; Windows error %lu.\n",
            operation, port_name, (unsigned long)code);
    fprintf(stderr,
        "Error 2: port not found. Error 5: port busy/access denied.\n"
        "Use COM8 here and its paired COM9 in the screen simulator.\n");
    if (serial_handle != INVALID_HANDLE_VALUE)
        CloseHandle(serial_handle);
    exit(1);
}

static void flush_tx(void)
{
    DWORD offset = 0;
    while (offset < tx_used) {
        DWORD written = 0;
        if (!WriteFile(serial_handle, tx_buffer + offset,
                       tx_used - offset, &written, NULL))
            fail("WriteFile");
        if (written == 0) {
            SetLastError(ERROR_TIMEOUT);
            fail("Serial write timeout");
        }
        offset += written;
        tx_total += written;
    }
    tx_used = 0;
}

void SendChar(uchar value)
{
    if (tx_used == sizeof(tx_buffer))
        flush_tx();
    tx_buffer[tx_used++] = value;
}

void UartInit(uint32 baud)
{
    char device[64];
    DCB dcb;
    COMMTIMEOUTS timeouts;

    snprintf(device, sizeof(device), "\\\\.\\%s", port_name);
    serial_handle = CreateFileA(device, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (serial_handle == INVALID_HANDLE_VALUE)
        fail("Open serial port");

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial_handle, &dcb))
        fail("GetCommState");

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(serial_handle, &dcb))
        fail("SetCommState");

    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.WriteTotalTimeoutConstant = 500;
    if (!SetCommTimeouts(serial_handle, &timeouts))
        fail("SetCommTimeouts");
    if (!PurgeComm(serial_handle, PURGE_RXCLEAR | PURGE_TXCLEAR))
        fail("PurgeComm");
}

/* Validate payload lengths before calling the existing dispatcher. */
static int valid_frame(const unsigned char *b, unsigned int length)
{
    unsigned int payload;
    if (length < 6)
        return 0;
    payload = length - 4;

    switch (b[1]) {
    case 0x07:
    case NOTIFY_HandShake:
    case NOTIFY_WRITE_FLASH_OK:
    case NOTIFY_WRITE_FLASH_FAILD:
    case NOTIFY_READ_FLASH_OK:
    case NOTIFY_READ_FLASH_FAILD:
        return 1;

    case NOTIFY_TOUCH_PRESS:
    case NOTIFY_TOUCH_RELEASE:
        return payload >= 6;

    case NOTIFY_READ_RTC:
        return payload >= 9;

    case NOTIFY_CONTROL:
        if (payload < 5)
            return 0;
        if (b[2] == MSG_GET_CURRENT_SCREEN)
            return 1;
        if (b[2] != MSG_GET_DATA || payload < 8)
            return 0;
        switch (b[7]) {
        case kCtrlButton:
        case kCtrlMenu:
            return payload >= 10;
        case kCtrlProgress:
        case kCtrlSlider:
        case kCtrlMeter:
            return payload >= 12;
        case kCtrlSelector:
            return payload >= 9;
        case kCtrlText:
        case kCtrlRTC:
            return 1;
        default:
            return 0;
        }

    default:
        return 0;
    }
}

static void reset_application(void)
{
    queue_reset();
    timer_tick_count = 0;
    App_Init();
    flush_tx();
    QueryPerformanceCounter(&epoch);
    puts("Application initialized.");
}

static void dispatch_frame(unsigned int length)
{
    unsigned int i;
    qsize size;

    if (!valid_frame(rx_frame, length)) {
        printf("Dropped unsupported/short frame: %u bytes\n", length);
        return;
    }

    /* A complete bounded frame fits in the existing 512-byte queue. */
    queue_reset();
    memset(cmd_buffer, 0, sizeof(cmd_buffer));
    for (i = 0; i < length; ++i)
        queue_push(rx_frame[i]);

    size = queue_find_cmd(cmd_buffer, CMD_MAX_SIZE);
    if (size != length) {
        puts("Frame parser rejected input.");
        return;
    }

    printf("RX command=0x%02X length=%u\n", cmd_buffer[1], length);
    if (cmd_buffer[1] == 0x07) {
        reset_application();
        return;
    }

    /* Bound text callbacks to this frame; remove its first tail byte. */
    if (cmd_buffer[1] == NOTIFY_CONTROL &&
        cmd_buffer[2] == MSG_GET_DATA &&
        cmd_buffer[7] == kCtrlText)
        cmd_buffer[length - 4] = '\0';

    ProcessMessage((PCTRL_MSG)cmd_buffer, size);
}

/* Discard overlong frames through their tail, without using stale data. */
static void receive_byte(unsigned char value)
{
    if (rx_used == 0 && value != 0xEE)
        return;
    rx_last_ms = GetTickCount64();

    if (rx_used < CMD_MAX_SIZE)
        rx_frame[rx_used] = value;
    if (rx_used <= CMD_MAX_SIZE)
        ++rx_used;

    rx_tail = (rx_tail << 8) | value;
    if (rx_tail == 0xFFFCFFFFUL) {
        if (rx_used <= CMD_MAX_SIZE)
            dispatch_frame(rx_used);
        else
            puts("Dropped overlong frame.");
        rx_used = 0;
        rx_tail = 0;
    }
}

static void poll_rx(void)
{
    unsigned char buffer[256];
    unsigned int batch;
    for (batch = 0; batch < 4; ++batch) {
        DWORD received = 0, i;
        if (!ReadFile(serial_handle, buffer, sizeof(buffer),
                      &received, NULL))
            fail("ReadFile");
        if (received == 0)
            break;
        rx_total += received;
        for (i = 0; i < received; ++i)
            receive_byte(buffer[i]);
    }

    if (rx_used && GetTickCount64() - rx_last_ms > 500) {
        rx_used = 0;
        rx_tail = 0;
        puts("Dropped incomplete frame after 500 ms.");
    }
}

static BOOL WINAPI console_event(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&stop_requested, 1);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    ULONGLONG next_report;
    unsigned int i;

    if (argc > 1)
        port_name = argv[1];
    if (argc > 2 || strlen(port_name) < 4 ||
        strlen(port_name) > 20 || _strnicmp(port_name, "COM", 3)) {
        fprintf(stderr, "Usage: SPO2_PCSim.exe [COM8]\n");
        return 2;
    }
    for (i = 3; port_name[i]; ++i) {
        if (!isdigit((unsigned char)port_name[i])) {
            fprintf(stderr, "Invalid COM port name.\n");
            return 2;
        }
    }

    if (!QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        fprintf(stderr, "High-resolution clock unavailable.\n");
        return 1;
    }

    SetConsoleCtrlHandler(console_event, TRUE);
    UartInit(115200);
    printf("SPO2 PC simulation: %s, 115200 8N1. Ctrl+C stops.\n",
           port_name);
    puts("Measurements come from the existing demonstration generator.");
    Sleep(300);
    reset_application();
    next_report = GetTickCount64() + 1000;

    while (!InterlockedCompareExchange(&stop_requested, 0, 0)) {
        LARGE_INTEGER now;
        uint32 due;
        unsigned int steps = 0;

        poll_rx();
        QueryPerformanceCounter(&now);
        due = (uint32)(((now.QuadPart - epoch.QuadPart) * 100) /
                       frequency.QuadPart);

        /* Deliver each 10 ms step, including ticks delayed by scheduling.
           Limit each batch so serial input is serviced during catch-up. */
        while ((uint32)(due - timer_tick_count) != 0 && steps < 20) {
            ++timer_tick_count;
            App_Tick();
            ++steps;
        }
        flush_tx();

        if (GetTickCount64() >= next_report) {
            printf("tick=%lu screen=%u running=%u TX=%lu RX=%lu\n",
                timer_tick_count,
                (unsigned int)App_CurrentScreen(),
                (unsigned int)App_IsRunning(), tx_total, rx_total);
            next_report = GetTickCount64() + 1000;
        }
        Sleep(1);
    }

    flush_tx();
    CloseHandle(serial_handle);
    return 0;
}