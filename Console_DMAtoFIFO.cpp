// Console_DMAtoFIFO.cpp : Defines the entry point for the console application.
// 
// It is assumed that PORT0 has wires looping it back to PORT1.
// 1. Setup output buffer of size handle->board_info.fifo_size - 0x400
// 2. Setup input buffer  of size handle->board_info.fifo_size - 0x400
// 3. SET FIFO 0 to input from PCI (DMA mem to board) and output on PCI read 
// 4. SET FIFO 1 to input from PORT1 and output to PCI (DMA board to mem)
// 5. Set Programmable Clock 0 to 100Khz.
// 6. Set DMA0 to output to FIFO 0 
// 7. Wait for DMA0 to complete
// 8. Clock output of FIFO 0 once to get first FIFO value onto PORT 0 pins
// 9. Set FIFO0 output clock to Programmable Clock 0
// 10. Set DMA1 to input from FIFO 1 and start the DMA engine.
// 11. Start Programmable Clock 0
// 12. Wait for DMA1 to complete.
// 13. Disable Programmable Clock 0
// 14. Compare input to output.
// 
#include "stdafx.h"
#include "DMX820_Console_Select.h"

#include <stdio.h>
#include <conio.h>

DMX820_Board_Handle handle;
int g_BoardIndex;
uint16* sent_data = NULL;
uint16* received_data = NULL;
DMX820_FifoCh_Config m_fifo_0_conf;
DMX820_FifoCh_Config m_fifo_1_config;
DMX820_PgmClk_Config m_pgmclk_config;

BOOL bufferFilled;
BOOL callbackCalled = FALSE;
DMX820_Error callback_error = DMX820_ERROR_NO_ERROR;
DMX820_Intrfc_DMA_Result dma_result;
CRITICAL_SECTION crit_section;


void dma_done_callback(DMX820_DMA_Callback_Info info)
{
    EnterCriticalSection(&crit_section);

    callbackCalled = TRUE;

    // Check the callback info to make sure it's good
    if(info.result != DMX820_ERROR_NO_ERROR)
    {
        // Record the error for the main program to display
        callback_error = info.result;
    }
    else
    {
        // Record how this DMA transfer finished
        dma_result = info.request_result;

        // If this was a successful transfer, mark the buffer to be logged to
        //  the file
        if(info.request_result == DMX820_INTRFC_DMA_RESULT_SUCCESS)
        {
            // Notify the main thread this DMA block is written
            bufferFilled = TRUE;
        }
    }

    LeaveCriticalSection(&crit_section);
}

int _tmain(int argc, _TCHAR* argv[])
{
	DMX820_Error result;
    BOOL is_done;

	int fail_at;

	uint32 DATA_SET_SIZE;


	if (DMX820_Select_SelectBoard(&g_BoardIndex))
	{
		printf("Board Selected!!!\n");
	}
	else
	{
		return -1;
	}
	// Now open the selected board
    result = DMX820_General_Open_Board(g_BoardIndex, &handle);
    if(result != DMX820_ERROR_NO_ERROR)
    {
        printf("Board could not be opened.\nError Code: %d",result);

        return -1;
    }

	// the board is opened, display the data 
	if (DMX820_Select_PrintBoardInfo(handle))
	{
		printf("\nHOORAY\n");
	}

    InitializeCriticalSection(&crit_section);

	// with the open board configurate the system to transfer 
	// a FIFO amount of data into the first FIFO.
	// check if the FIFO is full and read the data out of the FIFO
	// to test if it is actually working.
	do {
		// Disable Fifos
		printf("Disable FIFO 0\n");
		result = DMX820_FifoCh_Set_Enable(  handle,
											0,
											FALSE);
		if(result != DMX820_ERROR_NO_ERROR)
		{
			printf( "Failed disabling Fifo 0.\nError Code: %d",
						result);
			break;
		}

		printf("Disable FIFO 1\n");
		result = DMX820_FifoCh_Set_Enable(  handle,
											1,
											FALSE);
		if(result != DMX820_ERROR_NO_ERROR)
		{
			printf( "Failed disabling Fifo 1.\nError Code: %d",
						result);
			break;
		}

        // Disable PgmClk 0
		printf("Disable Programmable Clock 0\n");
        result = DMX820_PgmClk_Set_Mode(handle,
                                        0,
                                        DMX820_PGMCLK_MODE_DISABLED);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed disabling Pgmclk 0.\nError Code: %d",
                        result);
            break;
        }

        // Check DMA Buffers for Fifo 0       
        if(handle->board_info.fifo_size == 0)
        {
            printf( "Error no DMA buffers available for Fifo 0.\n");
            break;
        }
        // Setup to make sure this completes without timeout
		// by using the complete fifo_size minus 1024K
		DATA_SET_SIZE = handle->board_info.fifo_size - 0x400;

        // Allocate buffers
		printf("Allocating buffer to send data to the DMX820.\n");
        sent_data = new uint16[DATA_SET_SIZE];
        if(!sent_data)
        {
            printf( "Failed allocating sending buffer.\nError Code: %d",
                        result);
            break;
        }
       
        // Fill the data buffer with random data
		printf("Creating Random Data\n");
        for(uint32 i = 0; i < DATA_SET_SIZE; ++i)
        {
            sent_data[i] = ((rand() * 0xFFFF) / RAND_MAX);
        }
		sent_data[0] = 0xA11a;
		sent_data[1] = 0xA22a;
		sent_data[2] = 0xA33a;
		sent_data[3] = 0xA44a;
		sent_data[4] = 0xA55a;

		printf("Allocate buffer to receive data from the DMX820.\n");
		received_data = new uint16[DATA_SET_SIZE+4];	// new receive buffer + a few extra samples.
		if (!received_data)
		{
			printf("Failed allocating receiver buffer.Error Code: %d",
                        result);
            break;
		}
		memset((void*)received_data,0,DATA_SET_SIZE * 2);


        // Configure StdIO
        //  Port 1 as input
		printf("Configure Port 1 as Input (TO FIFO 1).\n");
        result = DMX820_StdIO_Set_IO_Mode(  handle,
                                            0,
                                            DMX820_STDIO_PORT_1,
                                            0xFFFF,
                                            DMX820_STDIO_MODE_INPUT);

        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring DIO port 1.\nError Code: %d.",
                        result);
            break;
        }
        
        // Port 0 as Fifo 0 peripheral out
		printf("Configure Port 0 as FIFO0 peripheral output.\n");
        result = DMX820_StdIO_Set_IO_Mode(  handle,
                                            0,
                                            DMX820_STDIO_PORT_0,
                                            0xFFFF,
                                            DMX820_STDIO_MODE_PER_OUT);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring DIO port 0.\nError Code: %d",
                        result);
            break;
        }
        
        result = DMX820_StdIO_Set_Periph_Mode(  handle,
                                                0,
                                                DMX820_STDIO_PORT_0,
                                                0xFFFF,
                                                DMX820_STDIO_PERIPH_FIFO_0);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf( "Failed setting peripheral for DIO port 0.\n"
                            "Error Code: %d",
                        result);
            break;
        }


        // Configure Fifos
		// Loading configuration for Fifo 0                            //RATNESH: structure data type is being used with Typedef. Just search user defined variables for their definitions.
		m_fifo_0_conf.in_clock = DMX820_FIFOCH_CLK_WRITE_PORT;         //RATNESH: pinkOnes:> instead of using confusing register values, they 'defined MACROS' everywhere, for better understanding.
		m_fifo_0_conf.out_clock = DMX820_FIFOCH_CLK_READ_PORT;
		m_fifo_0_conf.DREQ_source = DMX820_FIFOCH_DREQ_WRITE;
		m_fifo_0_conf.input_data = DMX820_FIFOCH_INPUT_PCI;
		printf("Configure FIFO 0\n");
        result = DMX820_FifoCh_Set_Config(  handle,
                                            0,
                                            m_fifo_0_conf);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring Fifo 0.\nError Code: %d",
                        result);
            break;
        }

		m_fifo_1_config.in_clock = DMX820_CONF_CLOCKBUS_PGMCLK_0;
		m_fifo_1_config.out_clock = DMX820_FIFOCH_CLK_READ_PORT;
		m_fifo_1_config.DREQ_source = DMX820_FIFOCH_DREQ_READ;
		m_fifo_1_config.input_data = DMX820_FIFOCH_INPUT_PORT1;
		printf("Configure FIFO 1\n");
        result = DMX820_FifoCh_Set_Config(  handle,
                                            1,
                                            m_fifo_1_config);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring Fifo 1.\nError Code: %d",
                        result);
            break;
        }

        // Configure PgmClk 0                                   //RATNESH: structure is being used with Typedef. Search for definitions.
		m_pgmclk_config.master = DMX820_CONF_CLOCKBUS_25_MHZ;
		m_pgmclk_config.start = DMX820_PGMCLK_CLOCK_IMMEDIATE;
		m_pgmclk_config.stop = DMX820_PGMCLK_CLOCK_NO_STOP;
		m_pgmclk_config.period = 93; // Set for 100Khz
		printf("Configure Programmable Clock 0 -> 100Khz\n");
        result = DMX820_PgmClk_Set_Config(  handle,
                                            0,
                                            m_pgmclk_config);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring Pgmclk 0.\nError Code: %d",
                        result);
            break;
        }


        // Port 0 as Fifo 0 peripheral out
        result = DMX820_StdIO_Set_IO_Mode(  handle,
                                            0,
                                            DMX820_STDIO_PORT_0,
                                            0xFFFF,
                                            DMX820_STDIO_MODE_PER_OUT);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf( "Failed configuring DIO port 0.\nError Code: %d",
                        result);
            break;
        }

		// continue connecting up the DMA with the callback function.
		printf("DMA CHANNEL 0 Callback about to be Installed\n");
		result = DMX820_DMA_Install_Callback( handle,
                                            DMX820_DMA_CHANNEL_0,
                                            dma_done_callback);

        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed installing DMA callback.\nError Code: %d",
                        result);
            break;
        }

        // Enable Fifo 0
		printf("FIFO 0 will now be enabled\n");
        result = DMX820_FifoCh_Set_Enable(  handle,
                                            0,
                                            TRUE);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed enabling Fifo 0.\nError Code: %d",
                        result);
            break;
        }

        // Queue the transfer
		printf("Request DMA 0 Start\n");
        result = DMX820_DMA_Request_Transfer(   handle,
                                                DMX820_DMA_CHANNEL_0,
                                                DMX820_DMA_OP_BUFFER_TO_BOARD,
                                                DMX820_FIFO0_RW_PORT,
                                                (void *)sent_data,
                                                DATA_SET_SIZE,
                                                TRUE,
                                                0,
                                                0,
                                                FALSE,
                                                NULL);

        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Queuing DMA transfer failed. Error %u\n",
                        result);
            break;
        }

		printf("Waiting for DMA to generate callback\n");
		is_done = FALSE;
        do
        {
		    EnterCriticalSection(&crit_section);
			if (callbackCalled)
			{
				if (bufferFilled)
				{
					is_done = TRUE;
				}
				else
				{
					// we failed ... .ooops
					printf("Failed waiting for Fifo 0.\n");
				    LeaveCriticalSection(&crit_section);
					break;
				}
			}
		    LeaveCriticalSection(&crit_section);

        } while(!is_done);
        
		printf("DMA Wait has ended\n");
		//
		// Clean up
		printf("Remove FIFO0 DMA Callback \n");
		result = DMX820_DMA_Remove_Callback(handle, DMX820_DMA_CHANNEL_0);
		if(result != DMX820_ERROR_NO_ERROR)
		{
			printf("Removing DMA callback Failed. Error %u\n", result);
		}

		// Clock output of FIFO 0 once to get the first FIFO value into place
		result = DMX820_FifoCh_Get_Data(handle,
										0,
										&received_data[0]);
		if(result != DMX820_ERROR_NO_ERROR)
		{
			printf("Failed priming Fifo 0.\nError Code: %d",
						result);
			break;
		}

		// Set output of FIFO 0 to Programmable Clock 0
		m_fifo_0_conf.in_clock = DMX820_FIFOCH_CLK_WRITE_PORT;
		m_fifo_0_conf.out_clock = DMX820_CONF_CLOCKBUS_PGMCLK_0;
		m_fifo_0_conf.DREQ_source = DMX820_FIFOCH_DREQ_WRITE;
		m_fifo_0_conf.input_data = DMX820_FIFOCH_INPUT_PCI;
		printf("Configure FIFO 0\n");
        result = DMX820_FifoCh_Set_Config(  handle,
                                            0,
                                            m_fifo_0_conf);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed configuring Fifo 0.\nError Code: %d",
                        result);
            break;
        }
		
		// Reset Callback system
		callbackCalled = FALSE;
		callback_error = DMX820_ERROR_NO_ERROR;
		bufferFilled = FALSE;
		printf("DMA CHANNEL 1 Callback Installed\n");
		result = DMX820_DMA_Install_Callback( handle,
                                            DMX820_DMA_CHANNEL_1,
                                            dma_done_callback);

        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed installing DMA callback.\nError Code: %d",
                        result);
            break;
        }

		// Enable FIFO1
		printf("FIFO 1 will now be enabled\n");
        result = DMX820_FifoCh_Set_Enable(  handle,
                                            1,
                                            TRUE);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed enabling Fifo 1.\nError Code: %d",
                        result);
            break;
        }

		// Start DMA1 
        // Queue the transfer
		// NOTE: This works because we are an even multiple of the PCI packet size.
		// If the data is an odd type, there will need to be an operation to test 
		// if there is still data in the FIFO and retrieve it. This could be accomplished
		// by changing the DREQ to FIFO NOT EMPTY mode.
		printf("Request DMA 1 Start\n");
        result = DMX820_DMA_Request_Transfer(   handle,
                                                DMX820_DMA_CHANNEL_1,
                                                DMX820_DMA_OP_BOARD_TO_BUFFER,
                                                DMX820_FIFO1_RW_PORT,
                                                (void *)received_data,
                                                DATA_SET_SIZE,
                                                TRUE,
                                                0,
                                                0,
                                                FALSE,
                                                NULL);

        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Queuing DMA transfer failed. Error %u\n",
                        result);
            break;
        }

		// Start programmable clock
		printf("Enable Programmable Clock\n");
		result = DMX820_PgmClk_Set_Mode(handle,
										0,
										DMX820_PGMCLK_MODE_CONT);

		// wait for the DMA to complete
		printf("Waiting for DMA to generate callback\n");
		is_done = FALSE;
        do
        {
		    EnterCriticalSection(&crit_section);
			if (callbackCalled)
			{
				if (bufferFilled)
				{
					is_done = TRUE;
				}
				else
				{
					// we failed ... .ooops
					printf("Failed waiting for Fifo 0.\n");
				    LeaveCriticalSection(&crit_section);
					break;
				}
			}
		    LeaveCriticalSection(&crit_section);

        } while(!is_done);

		printf("DMA Wait has ended\n");
		// Stop programmable clock
		printf("Disable Programmable Clock\n");
		result = DMX820_PgmClk_Set_Mode(handle,
										0,
										DMX820_PGMCLK_MODE_DISABLED);
        if(result != DMX820_ERROR_NO_ERROR)
        {
            printf("Failed disabling Pgmclk 0.\nError Code: %d",
                        result);
            break;
        }

		// Test the data 
		is_done = TRUE;
		fail_at = -1;
        for(uint32 i = 0; i < DATA_SET_SIZE; ++i)
        {
			if (i <10){
			printf(">> %d >>> %04x < %04x >\n",i,received_data[i],sent_data[i]);
			}
			if (received_data[i] != sent_data[i]) 
			{
				is_done= FALSE;
				fail_at = i;
				break;}
		}

		if (is_done)
		{
			printf("Successfully transferred %d words from memory to the FIFO\n",
				   DATA_SET_SIZE);
		}
		else
		{
			printf("Failed to transfer all data. Data was wrong at index %d\n",fail_at);
			for (int i = fail_at; i < (fail_at +10); i++)
			{
				printf(">> %d >>> %04x < %04x >\n",i,received_data[i],sent_data[i]);
			}
		}

	} while (0);

	printf("Disable Programmable Clock\n");
    result = DMX820_PgmClk_Set_Mode(handle,
                                    0,
                                    DMX820_PGMCLK_MODE_DISABLED);

	printf("Remove Callback \n");
	result = DMX820_DMA_Remove_Callback(handle, DMX820_DMA_CHANNEL_1);
	if(result != DMX820_ERROR_NO_ERROR)
	{
		printf("Removing DMA callback Failed. Error %u\n", result);
	}

    // Disable Fifos
	printf("Disable FIFO \n");
    DMX820_FifoCh_Set_Enable(handle, 0, FALSE);
	DMX820_FifoCh_Set_Enable(handle, 1 ,FALSE);

	printf("Closing Board\n");
    DMX820_General_Close_Board(handle);

    DeleteCriticalSection(&crit_section);

    // Delete memory
	printf("Unloading allocated memory\n");
    if(sent_data)
        delete[] sent_data;
    printf("Press any key to continue...\n\n\n");

    while(_kbhit()) _getch();

    while(!_kbhit());
    _getch();

	return 0;
}

