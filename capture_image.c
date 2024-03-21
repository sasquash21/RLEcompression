#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "hps_soc_system.h"
#include "socal.h"
// #include "address_map_arm.h"
#include "hps.h"

#define KEY_BASE			0xFF200050
#define VIDEO_IN_BASE		0xFF203060
#define FPGA_ONCHIP_BASE	0x08000000

#define X_RES				320
#define Y_RES				240

#define HPS_GPIO1_BASE		0xFF709000
#define SW_BASE				0xFF200040
#define SDRAM_BASE       	0x00000000

volatile int 	* SDRAM_ptr 	= (int *) SDRAM_BASE;
volatile int 	* HPS_GPIO1_ptr = (int *) HPS_GPIO1_BASE;
volatile int 	* KEY_ptr		= (int *) KEY_BASE;

volatile int 	* Video_In_DMA_ptr	= (int *) VIDEO_IN_BASE;
volatile short 	* Video_Mem_ptr	= (short *) FPGA_ONCHIP_BASE;

int i, x, y; 							// temp variables for the pixels
int THRESHOLD = 0; 						// threshold for black and white conversion
long long int avg_sum = 0; 				// sum of all pixel values

short raw_array[X_RES][Y_RES];				// array for storing raw image
short oned_array[X_RES*Y_RES];				// 1d array representing the image
int compressed[X_RES * Y_RES * 2];  	// Compressed 1d image representation
short decompressed[X_RES][Y_RES]; 				// decompressed image
unsigned char p_bytes[(320*240)/8]; 	// store one bit per pixel in groups of 8 bits

int comp = 0; 							// number of bytes compressed
int decomp = 0;							// number of bytes decompressed
int sdram_index = 0;					// index of SDRAM


int main(void) {
	*(Video_In_DMA_ptr + 3)	= 0x4; // Enable the video

	while (1) {

		// calculate memory address of character buffer
		volatile char *char_start = (char *)0x09000000; // character buffer address
		int offset = (1 << 9) + 3;
        char* text_ptr = "                          ";
        offset = (1 << 9) + 3;
		// write to character buffer
		while ( *(text_ptr) ){
			*(char_start+offset) = *(text_ptr);
			++text_ptr;
			++offset;
		}
		// poll button to stop video stream
		while (1)
		{
			if (*KEY_ptr != 0)						// check if any KEY was pressed
			{
				*(Video_In_DMA_ptr + 3) = 0x0;		// disable the video to capture one frame
				while (*KEY_ptr != 0);				// wait for pushbutton KEY release
				break;
			}
		}

		// capture image frame
		for (y = 0; y < Y_RES; y++) {
			for (x = 0; x < X_RES; x++) {
				raw_array[x][y] = *(Video_Mem_ptr + (y << 9) + x);  // reading the pixel value
			}
		}

		// poll button to stop video stream
		while (1)
		{
			if (*KEY_ptr != 0)						// check if any KEY was pressed
			{
				*(Video_In_DMA_ptr + 3) = 0x0;		// Disable the video to capture one frame
				while (*KEY_ptr != 0);				// wait for pushbutton KEY release
				break;
			}
		}

		// get THRESHOLD for image
		avg_sum = 0;

		for (y = 0; y < Y_RES; y++) {
			for (x = 0; x < X_RES; x++) {
				avg_sum += raw_array[x][y]; // sum up the pixel values
			}
		}

		THRESHOLD = avg_sum/76800;	// divide sum by the number of pixels

		// convert image to black and white
		for (y = 0; y < Y_RES; y++) {
			for (x = 0; x < X_RES; x++) {
				if(raw_array[x][y] > THRESHOLD) raw_array[x][y] = 0xFFFF;
				else raw_array[x][y] = 0x0000;
			}
		}

		// show input image again
		for (y = 0; y < Y_RES; y++) {
			for (x = 0; x < X_RES; x++) {
				// display the image
				*(Video_Mem_ptr + (y << 9) + x) = raw_array[x][y]; // writing it back into the memory location
			}
		}

		int index = 0;
		// write to a 1d array
		for (y = 0; y < Y_RES; y++){
			for(x = 0; x < X_RES; x++){
				oned_array[index] = raw_array[x][y];
				index++;
			}
		}

	    // Perform compression
		int compressedSize = compressRLE((short*)oned_array, compressed);
		printf("Compressing....\n");
		// Display the compressed data
		for (int i = 0; i < compressedSize; i++) {
			printf("%d ", compressed[i]);
		}


		// poll button
		while (1)
		{
			if (*KEY_ptr != 0)						// check if any KEY was pressed
			{
				*(Video_In_DMA_ptr + 3) = 0x0;		// Disable the video to capture one frame
				while (*KEY_ptr != 0);				// wait for pushbutton KEY release
				break;
			}
		}

		//unsigned char decompressed[X_RES][Y_RES];     // Decompressed image

		// Perform decompression
		printf("\n\nDecompressing...\n");
		decompressRLE(compressed, compressedSize);

        text_ptr = "Decompressed Image";
        offset = (1 << 9) + 3;
		// write to character buffer
		while ( *(text_ptr) ){
			*(char_start+offset) = *(text_ptr);
			++text_ptr;
			++offset;
		}
		// show input image again
		for (y = 0; y < Y_RES; y++) {
			for (x = 0; x < X_RES; x++) {
				// display the image
				*(Video_Mem_ptr + (y << 9) + x) = decompressed[x][y]; // writing it back into the memory location
			}
		}

		float ratio = (float)(X_RES*Y_RES*16)/(compressedSize*24/2); 	// compression ratio
		printf("Compression Ratio %.4f\n", ratio);

		// poll button to restart video stream
		while (1)
		{
			if (*KEY_ptr != 0)						// check if any KEY was pressed
			{
				*(Video_In_DMA_ptr + 3) = 0x4;		// Disable the video to capture one frame
				while (*KEY_ptr != 0);				// wait for pushbutton KEY release
				break;
			}
		}
	}
}

int compressRLE(short oned_array[], int *compressed) {
    int count = 1;          // Number of consecutive pixels with the same value
    int compressedSize = 0; // Size of the compressed data

    for (int i = 0; i < X_RES*Y_RES; i++) {

            if (oned_array[i] == oned_array[i-1]) {
                count++;
            } else {
                // Store the count and pixel value in the compressed array
                compressed[compressedSize] = count;
                compressed[compressedSize+1] = oned_array[i-1];
                count = 1;
                compressedSize += 2;
            }
        }
    compressed[compressedSize] = count-1;
    compressed[compressedSize+1] = oned_array[i-1];
    compressedSize += 2;

    return compressedSize;
}

// Function to perform RLE decompression
void decompressRLE(int compressed[], int compressedSize) {

	int compressedIndex = 0;
	int decompressedIndex = 0;
	short decompressed_1d[X_RES*Y_RES];

	int i =0;

	while(compressedIndex < compressedSize){
		int count = compressed[compressedIndex];
		short data = compressed[compressedIndex+1];
		i = 0;

		while(i < count){
			decompressed_1d[decompressedIndex] = data;
			decompressedIndex++;
			i++;
		}
		compressedIndex += 2;
	}

    int index = 0;
    for (int y = 0; y < Y_RES; y++) {
    	for (int x = 0; x < X_RES; x++) {
    		decompressed[x][y] = decompressed_1d[index];
            index++;
        }
    }
}
