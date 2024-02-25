/*
 * WakeOnLAN v0.3
 * A simple C program that sends a magic packet
 *
 *
 * MIT License
 * 
 * Copyright (c) 2017 Grammatopoulos Athanasios-Vasileios
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */


#ifdef __linux
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
#endif

#include <stdio.h>
#include <string.h>
#include "WakeOnLAN.h"

// Create Magic Packet
static void createMagicPacket(unsigned char packet[], unsigned int macAddress[]){
	int i;
	// Mac Address Variable
	unsigned char mac[6];

	// 6 x 0xFF on start of packet
	for(i = 0; i < 6; i++){
		packet[i] = 0xFF;
		mac[i] = macAddress[i];
	}
	// Rest of the packet is MAC address of the pc
	for(i = 1; i <= 16; i++){
		memcpy(&packet[i * 6], &mac, 6 * sizeof(unsigned char));
	}
}

void WakeOnLAN(const char *mac_addr, const char *broadcast_addr){
	// Default broadcast address
	char const *broadcastAddress = "255.255.255.255";
	// Packet buffer
	unsigned char packet[102];
	// Mac address
	unsigned int mac[6];
	// Set broadcast
	int broadcast = 1;

	// Socket address
	struct sockaddr_in udpClient, udpServer;
	
	// Help variables
	int i = 0;
	
	// If no arguments
	if(!mac_addr)
		return;

	// Parse Mac Address
	i = sscanf(mac_addr,"%x:%x:%x:%x:%x:%x", &(mac[0]), &(mac[1]), &(mac[2]), &(mac[3]), &(mac[4]), &(mac[5]));
	if(i != 6)
		return;

	// Check if a broadcast address was given too
	if(broadcast_addr){
		// Parse Broadcast Address
		i = sscanf(broadcast_addr,"%d.%d.%d.%d", &i, &i, &i, &i);
		if(i == 4)
			broadcastAddress = broadcast_addr;
	}

	// Create Magic Packet
	createMagicPacket(packet, mac);


	// MacOS and Linux
	#if defined(__APPLE__) || defined(__linux)
		int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (udpSocket == -1)
			return;
		int setsock_result = setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast);
		if (setsock_result == -1)
			return;
		// Set parameters
		udpClient.sin_family = AF_INET;
		udpClient.sin_addr.s_addr = INADDR_ANY;
		udpClient.sin_port = 0;
		// Bind socket
		int bind_result = bind(udpSocket, (struct sockaddr*) &udpClient, sizeof(udpClient));
		if (bind_result == -1)
			return;

		// Set server end point (the broadcast addres)
		udpServer.sin_family = AF_INET;
		udpServer.sin_addr.s_addr = inet_addr(broadcastAddress);
		udpServer.sin_port = htons(9);

		// Send the packet
		for (int count = 0; count < 5; count++) {
			int result = sendto(udpSocket, &packet, sizeof(unsigned char) * 102, 0, (struct sockaddr*) &udpServer, sizeof(udpServer));
			if (result == -1)
				return;
		}
	#endif
}