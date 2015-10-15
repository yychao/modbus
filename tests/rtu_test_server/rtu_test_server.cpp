// rtu_test_server.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "rtu_test_server.h"
#include <windows.h>
#include <commctrl.h>

#include "modbus.h"
#include "unit-test.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// The one and only application object

CWinApp theApp;

using namespace std;


enum {
    TCP,
    TCP_PI,
    RTU
};

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	int nRetCode = 0;

	// initialize MFC and print and error on failure
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		// TODO: change error code to suit your needs
		_tprintf(_T("Fatal Error: MFC initialization failed\n"));
		nRetCode = 1;
	}
	else
	{
        // TODO: code your application's behavior here.
        modbus_t *ctx;
        modbus_mapping_t *mb_mapping;
        int rc;
        int i;
        int use_backend = RTU;
        uint8_t *query;
        int header_length;

        ctx = modbus_new_rtu(_T("COM1:"), 115200, _T('N'), 8, 1);
        modbus_set_slave(ctx, SERVER_ID);
        query = (uint8_t *)malloc(MODBUS_RTU_MAX_ADU_LENGTH);
        header_length = modbus_get_header_length(ctx);
        modbus_set_debug(ctx, TRUE);

        mb_mapping = modbus_mapping_new(
                                        UT_BITS_ADDRESS + UT_BITS_NB,
                                        UT_INPUT_BITS_ADDRESS + UT_INPUT_BITS_NB,
                                        UT_REGISTERS_ADDRESS + UT_REGISTERS_NB,
                                        UT_INPUT_REGISTERS_ADDRESS + UT_INPUT_REGISTERS_NB);
        if (mb_mapping == NULL) {
            mb_fprintf(_T("Failed to allocate the mapping: %s\n"),
                    modbus_strerror(errno));
            modbus_free(ctx);
            return -1;
        }

        /* Unit tests of modbus_mapping_new (tests would not be sufficient if two
           nb_* were identical) */
        if (mb_mapping->nb_bits != UT_BITS_ADDRESS + UT_BITS_NB) {
            printf("Invalid nb bits (%d != %d)\n", UT_BITS_ADDRESS + UT_BITS_NB, mb_mapping->nb_bits);
            modbus_free(ctx);
            return -1;
        }

        if (mb_mapping->nb_input_bits != UT_INPUT_BITS_ADDRESS + UT_INPUT_BITS_NB) {
            printf("Invalid nb input bits: %d\n", UT_INPUT_BITS_ADDRESS + UT_INPUT_BITS_NB);
            modbus_free(ctx);
            return -1;
        }

        if (mb_mapping->nb_registers != UT_REGISTERS_ADDRESS + UT_REGISTERS_NB) {
            printf("Invalid nb registers: %d\n", UT_REGISTERS_ADDRESS + UT_REGISTERS_NB);
            modbus_free(ctx);
            return -1;
        }

        if (mb_mapping->nb_input_registers != UT_INPUT_REGISTERS_ADDRESS + UT_INPUT_REGISTERS_NB) {
            printf("Invalid nb input registers: %d\n", UT_INPUT_REGISTERS_ADDRESS + UT_INPUT_REGISTERS_NB);
            modbus_free(ctx);
            return -1;
        }

        /* Examples from PI_MODBUS_300.pdf.
           Only the read-only input values are assigned. */

        /** INPUT STATUS **/
        modbus_set_bits_from_bytes(mb_mapping->tab_input_bits,
                                   UT_INPUT_BITS_ADDRESS, UT_INPUT_BITS_NB,
                                   UT_INPUT_BITS_TAB);

        /** INPUT REGISTERS **/
        for (i=0; i < UT_INPUT_REGISTERS_NB; i++) {
            mb_mapping->tab_input_registers[UT_INPUT_REGISTERS_ADDRESS+i] =
                UT_INPUT_REGISTERS_TAB[i];;
        }

        rc = modbus_connect(ctx);
        if (rc == -1) {
            mb_fprintf(_T("Unable to connect %s\n"), modbus_strerror(errno));
            modbus_free(ctx);
            return -1;
        }

        for (;;) {
            do {
                rc = modbus_receive(ctx, query);
                /* Filtered queries return 0 */
            } while (rc == 0);

            /* The connection is not closed on errors which require on reply such as
               bad CRC in RTU. */
            if (rc == -1 && errno != EMBBADCRC) {
                /* Quit */
                break;
            }

            /* Special server behavior to test client */
            if (query[header_length] == 0x03) {
                /* Read holding registers */

                if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 3) == UT_REGISTERS_NB_SPECIAL) {
                    mb_fprintf(_T("Set an incorrect number of values\n"));
                    MODBUS_SET_INT16_TO_INT8(query, header_length + 3,
                                             UT_REGISTERS_NB_SPECIAL - 1);
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1) == UT_REGISTERS_ADDRESS_SPECIAL) {
                    mb_fprintf(_T("Reply to this special register address by an exception\n"));
                    modbus_reply_exception(ctx, query,
                                           MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
                    continue;
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1)
                           == UT_REGISTERS_ADDRESS_INVALID_TID_OR_SLAVE) {
                    const int RAW_REQ_LENGTH = 5;
                    uint8_t raw_req[] = {
                        (use_backend == RTU) ? INVALID_SERVER_ID : 0xFF,
                        0x03,
                        0x02, 0x00, 0x00
                    };

                    mb_fprintf(_T("Reply with an invalid TID or slave\n"));
                    modbus_send_raw_request(ctx, raw_req, RAW_REQ_LENGTH * sizeof(uint8_t));
                    continue;
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1)
                           == UT_REGISTERS_ADDRESS_SLEEP_500_MS) {
                    mb_fprintf(_T("Sleep 0.5 s before replying\n"));
                    Sleep(500);
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1) == UT_REGISTERS_ADDRESS_BYTE_SLEEP_5_MS) {
//                     /* Test low level only available in TCP mode */
//                     /* Catch the reply and send reply byte a byte */
//                     uint8_t req[] = "\x00\x1C\x00\x00\x00\x05\xFF\x03\x02\x00\x00";
//                     int req_length = 11;
//                     int w_s = modbus_get_socket(ctx);
// 
//                     /* Copy TID */
//                     req[1] = query[1];
//                     for (i=0; i < req_length; i++) {
//                         mb_fprintf(_T("(%.2X)"), req[i]);
//                         Sleep(5);
//                         send(w_s, (const char*)(req + i), 1, MSG_NOSIGNAL);
//                     }
                    continue;
                }
            }

            rc = modbus_reply(ctx, query, rc, mb_mapping);
            if (rc == -1) {
                break;
            }
        }

        mb_fprintf(_T("Quit the loop: %s\n"), modbus_strerror(errno));

        modbus_mapping_free(mb_mapping);
        free(query);
        /* For RTU */
        modbus_close(ctx);
        modbus_free(ctx);
	}

    //system("pause");

    char ch;
    ch = getchar();

	return nRetCode;
}
