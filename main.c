
/* SSDV - Slow Scan Digital Video                                        */
/*=======================================================================*/
/* Copyright 2011-2016 Philip Heron <phil@sanslogic.co.uk>               */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#else
char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

/* Minimal getopt() compatibility for Windows/MSVC builds. */
static int getopt(int argc, char * const argv[], const char *optstring)
{
	static int nextchar = 1;
	char *arg;
	const char *opt;
	char c;

	if(optind >= argc) return(-1);

	arg = argv[optind];
	if(nextchar == 1)
	{
		if(arg[0] != '-' || arg[1] == '\0') return(-1);
		if(arg[1] == '-' && arg[2] == '\0')
		{
			optind++;
			return(-1);
		}
	}

	c = arg[nextchar++];
	opt = strchr(optstring, c);

	if(!opt)
	{
		optopt = c;
		if(opterr) fprintf(stderr, "Unknown option: -%c\n", c);
		if(arg[nextchar] == '\0')
		{
			nextchar = 1;
			optind++;
		}
		return('?');
	}

	if(opt[1] == ':')
	{
		if(arg[nextchar] != '\0')
		{
			optarg = &arg[nextchar];
			nextchar = 1;
			optind++;
		}
		else if(optind + 1 < argc)
		{
			optarg = argv[optind + 1];
			nextchar = 1;
			optind += 2;
		}
		else
		{
			optopt = c;
			if(opterr) fprintf(stderr, "Option -%c requires an argument\n", c);
			nextchar = 1;
			optind++;
			return('?');
		}
	}
	else
	{
		optarg = NULL;
		if(arg[nextchar] == '\0')
		{
			nextchar = 1;
			optind++;
		}
	}

	return(c);
}
#endif
#include "ssdv.h"

#ifndef SSDV_VERSION
#define SSDV_VERSION "dev"
#endif

static int build_output_path(char *path, size_t path_len, const char *base_path, int image_index)
{
	const char *dot, *sep;

	if(image_index <= 1)
	{
		if(snprintf(path, path_len, "%s", base_path) >= (int) path_len) return(-1);
		return(0);
	}

	sep = strrchr(base_path, '/');
#ifdef _WIN32
	{
		const char *sep2 = strrchr(base_path, '\\');
		if(sep2 && (!sep || sep2 > sep)) sep = sep2;
	}
#endif
	dot = strrchr(base_path, '.');
	if(dot && sep && dot < sep) dot = NULL;

	if(dot)
	{
		if(snprintf(path, path_len, "%.*s_%d%s", (int) (dot - base_path), base_path, image_index, dot) >= (int) path_len) return(-1);
	}
	else
	{
		if(snprintf(path, path_len, "%s_%d", base_path, image_index) >= (int) path_len) return(-1);
	}

	return(0);
}

static int write_decoded_image(ssdv_t *ssdv, uint8_t **jpeg, size_t *jpeg_length, const char *base_output_path, int image_index, FILE *stdout_fallback)
{
	FILE *f = stdout_fallback;
	char path[1024];

	ssdv_dec_get_jpeg(ssdv, jpeg, jpeg_length);

	if(base_output_path)
	{
		if(build_output_path(path, sizeof(path), base_output_path, image_index) < 0)
		{
			fprintf(stderr, "Output path is too long\n");
			return(-1);
		}

		f = fopen(path, "wb");
		if(!f)
		{
			fprintf(stderr, "Error opening '%s' for output:\n", path);
			perror("fopen");
			return(-1);
		}
	}

	if(fwrite(*jpeg, 1, *jpeg_length, f) != *jpeg_length)
	{
		fprintf(stderr, "Error writing decoded image data\n");
		if(base_output_path) fclose(f);
		return(-1);
	}

	if(base_output_path)
	{
		fclose(f);
		fprintf(stderr, "Wrote image %d to '%s' (%lu bytes)\n", image_index, path, (unsigned long) *jpeg_length);
	}
	else
	{
		fflush(f);
	}

	return(0);
}

void exit_usage()
{
	fprintf(stderr,
		"\n"
		"Usage: ssdv [-e|-d] [-n] [-C] [-t <percentage>] [-c <callsign>] [-i <id>] [-q <level>] [-u <profile>] [-l <length>] [<in file>] [<out file>]\n"
		"\n"
		"  -e Encode JPEG to SSDV packets.\n"
		"  -d Decode SSDV packets to JPEG.\n"
		"  -v Print version and exit.\n"
		"\n"
		"  -n Encode packets with no FEC.\n"
		"  -C Encode CCSDS packets (246 bytes, for use with CCSDS space packet).\n"
		"  -t For testing, drops the specified percentage of packets while decoding.\n"
		"  -U <[ip:]port> Listen (decode) or transmit (encode) SSDV packets via UDP.\n"
		"  -c Set the callign. Accepts A-Z 0-9 and space, up to 6 characters.\n"
		"  -i Set the image ID (0-65535).\n"
		"  -q Set the JPEG quality level (0 to 7, defaults to 4).\n"
		"  -u Set Huffman profile for encoding: 0 = standard, 1 = optimized (default).\n"
		"  -l Set packet length in bytes (default 256).\n"
		"  -V Print data for each packet decoded.\n"
		"\n"
		"Packet Length\n"
		"\n"
		"The packet length must be specified for both encoding and decoding if not\n"
		"the default 256 bytes. Smaller packets will increase overhead.\n"
		"\n");
	exit(-1);
}

static inline int get_next_byte(FILE *fin, int sockfd, uint8_t *udp_buf, int *len, int *pos) {
	if (sockfd > 0) {
		if (*pos >= *len) {
			*len = recv(sockfd, (char*)udp_buf, 65536, 0);
			if (*len <= 0) return EOF;
			*pos = 0;
		}
		return udp_buf[(*pos)++];
	}
	return fgetc(fin);
}

int main(int argc, char *argv[])
{
	int c, i;
	FILE *fin = stdin;
	FILE *fout = stdout;
	const char *in_path = NULL;
	const char *out_path = NULL;
	const char *decode_output_base = NULL;
	char encode = -1;
	char type = SSDV_TYPE_NORMAL;
	int droptest = 0;
	int verbose = 0;
	int errors;
	int udp_port = 0;
	char udp_host[256] = "";
	char callsign[7];
	uint16_t image_id = 0;
	int8_t quality = 4;
	uint8_t huff_profile = 1;
	int pkt_length = SSDV_PKT_SIZE;
	ssdv_t ssdv;
	int skipped;
	uint8_t *pkt = NULL, b[128], *jpeg = NULL;
	size_t jpeg_length;
	
	callsign[0] = '\0';
#ifdef _WIN32
	SOCKET sockfd = INVALID_SOCKET;
#else
	int sockfd = -1;
#endif
	
	opterr = 0;
	while((c = getopt(argc, argv, "ednCvc:i:q:u:l:t:VU:")) != -1)
	{
		switch(c)
		{
		case 'e': encode = 1; break;
		case 'd': encode = 0; break;
		case 'v':
			fprintf(stdout, "ssdv-ng %s\n", SSDV_VERSION);
			return(0);
		case 'n': type = SSDV_TYPE_NOFEC; break;
		case 'C': type = SSDV_TYPE_CCSDS; pkt_length = SSDV_PKT_SIZE_CCSDS; break;
		case 'c':
			if(strlen(optarg) > 6)
			{
				fprintf(stderr, "Warning: callsign cropped to 6 characters.\n");
			}
			strncpy(callsign, optarg, 6);
			callsign[6] = '\0';
			break;
		case 'i': image_id = (uint16_t) atoi(optarg); break;
		case 'q': quality = atoi(optarg); break;
		case 'u': huff_profile = (uint8_t) atoi(optarg); break;
		case 'l': pkt_length = atoi(optarg); break;
		case 't': droptest = atoi(optarg); break;
		case 'V': verbose = 1; break;
		case 'U':
		{
			char *colon = strrchr(optarg, ':');
			if (colon) {
				int len = colon - optarg;
				if (len >= sizeof(udp_host)) len = sizeof(udp_host) - 1;
				strncpy(udp_host, optarg, len);
				udp_host[len] = '\0';
				udp_port = atoi(colon + 1);
			} else {
				udp_port = atoi(optarg);
			}
			break;
		}
		case '?': exit_usage();
		}
	}

	if (encode == 1 && pkt_length > SSDV_PKT_SIZE && type == SSDV_TYPE_NORMAL) {
		fprintf(stderr, "Error: Packets larger than %d bytes are only supported with no FEC (-n option) or CCSDS mode (-C).\n", SSDV_PKT_SIZE);
		return(-1);
	}
	
	c = argc - optind;
	if(c > 2) exit_usage();
	
	for(i = 0; i < c; i++)
	{
		switch(i)
		{
		case 0:
			in_path = argv[optind + i];
			break;
		
		case 1:
			out_path = argv[optind + i];
			break;
		}
	}

	if(in_path && strcmp(in_path, "-"))
	{
		fin = fopen(in_path, "rb");
		if(!fin)
		{
			fprintf(stderr, "Error opening '%s' for input:\n", in_path);
			perror("fopen");
			return(-1);
		}
	}

	if(encode == 1)
	{
		if(out_path && strcmp(out_path, "-"))
		{
			fout = fopen(out_path, "wb");
			if(!fout)
			{
				fprintf(stderr, "Error opening '%s' for output:\n", out_path);
				perror("fopen");
				if(fin != stdin) fclose(fin);
				return(-1);
			}
		}
	}
	else if(encode == 0)
	{
		if(out_path && strcmp(out_path, "-")) decode_output_base = out_path;
	}
	
	struct sockaddr_in destaddr;
	memset(&destaddr, 0, sizeof(destaddr));

	if (udp_port > 0)
	{
#ifdef _WIN32
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#else
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#endif
		if (sockfd < 0) {
			fprintf(stderr, "Failed to create UDP socket\n");
			return -1;
		}

		int bufsize = 4 * 1024 * 1024;
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char *)&bufsize, sizeof(bufsize)) < 0) {
			fprintf(stderr, "Warning: Failed to set UDP receive buffer size\n");
		}
		if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char *)&bufsize, sizeof(bufsize)) < 0) {
			fprintf(stderr, "Warning: Failed to set UDP send buffer size\n");
		}

		if (encode == 0) {
			uint32_t ip = udp_host[0] ? ntohl(inet_addr(udp_host)) : 0;
			int is_multicast = (ip >= 0xE0000000) && (ip <= 0xEFFFFFFF);

			int reuse = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

			struct sockaddr_in servaddr;
			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_addr.s_addr = (udp_host[0] && !is_multicast) ? inet_addr(udp_host) : INADDR_ANY;
			servaddr.sin_port = htons(udp_port);
			if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
				/* Fallback to 0.0.0.0 if explicit IP fails */
				servaddr.sin_addr.s_addr = INADDR_ANY;
				if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
					fprintf(stderr, "Failed to bind UDP socket to port %d\n", udp_port);
					return -1;
				}
			}
			
			/* Automatically join multicast group if IP is in the 224.0.0.0 - 239.255.255.255 range */
			if (is_multicast) {
				struct ip_mreq mreq;
				mreq.imr_multiaddr.s_addr = inet_addr(udp_host);
				mreq.imr_interface.s_addr = htonl(INADDR_ANY);
				if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) < 0) {
					fprintf(stderr, "Warning: Failed to join multicast group %s\n", udp_host);
				}
			}
			fprintf(stderr, "Listening for SSDV packets on UDP %s:%d...\n", udp_host[0] ? udp_host : "0.0.0.0", udp_port);
		} else {
			destaddr.sin_family = AF_INET;
			destaddr.sin_port = htons(udp_port);
			destaddr.sin_addr.s_addr = udp_host[0] ? inet_addr(udp_host) : inet_addr("127.0.0.1");
			fprintf(stderr, "Transmitting SSDV packets to UDP %s:%d...\n", udp_host[0] ? udp_host : "127.0.0.1", udp_port);
		}
	}

	pkt = malloc(pkt_length);
	if (!pkt) {
		fprintf(stderr, "Failed to allocate packet buffer\n");
		if(fin != stdin) fclose(fin);
		if(fout != stdout && encode == 1) fclose(fout);
		return -1;
	}

	
	switch(encode)
	{
	case 0: /* Decode */
	{
		int packets_total = 0;
		int packets_in_image = 0;
		int images_written = 0;
		int warned_stdout_multi = 0;
		
		if(droptest > 0) fprintf(stderr, "*** NOTE: Drop test enabled: %i ***\n", droptest);
		
		if(ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK)
		{
			free(pkt);
			return(-1);
		}
		
		jpeg_length = 1024 * 1024 * 4;
		jpeg = malloc(jpeg_length);
		if(!jpeg)
		{
			fprintf(stderr, "Failed to allocate decode buffer\n");
			free(pkt);
			if(fin != stdin) fclose(fin);
			return(-1);
		}
		ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
		
		uint8_t udp_buf[65536];
		int udp_buf_len = 0, udp_buf_pos = 0;
		
		for (i = 0; i < pkt_length; i++) {
			int next_byte = get_next_byte(fin, sockfd, udp_buf, &udp_buf_len, &udp_buf_pos);
			if (next_byte == EOF) goto end_of_stream;
			pkt[i] = (uint8_t)next_byte;
		}

		while(1)
		{
			ssdv_packet_info_t p;
			int feed_result;

			/* Drop % of packets */
			if(droptest && (rand() / (RAND_MAX / 100) < droptest)) continue;
			
			/* Test the packet is valid */
			skipped = 0;
			while(1)
			{
				if(pkt_length == SSDV_PKT_SIZE_CCSDS || pkt[0] == SSDV_PKT_SYNC)
				{
					if((c = ssdv_dec_is_packet(pkt, pkt_length, &errors)) == 0) break;
				}
				memmove(&pkt[0], &pkt[1], pkt_length - 1);
				int next_byte = get_next_byte(fin, sockfd, udp_buf, &udp_buf_len, &udp_buf_pos);
				if(next_byte == EOF)
				{
					c = -1;
					break;
				}
				pkt[pkt_length - 1] = (uint8_t)next_byte;
				skipped++;
			}
			/* No valid packet was found before EOF */
			if(c != 0) break;

			ssdv_dec_header(&p, pkt, pkt_length);

			/* New image start: flush and reset the previous image decoder state */
			if(p.packet_id == 0 && packets_in_image > 0)
			{
				images_written++;
				if(!decode_output_base && images_written > 1 && !warned_stdout_multi)
				{
					fprintf(stderr, "Warning: multiple images detected while writing to stdout; JPEG data is concatenated.\n");
					warned_stdout_multi = 1;
				}

				if(write_decoded_image(&ssdv, &jpeg, &jpeg_length, decode_output_base, images_written, fout) < 0)
				{
					free(pkt);
					free(jpeg);
					if(fin != stdin) fclose(fin);
					if(fout != stdout) fclose(fout);
					return(-1);
				}

				if(ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK)
				{
					free(pkt);
					free(jpeg);
					if(fin != stdin) fclose(fin);
					if(fout != stdout) fclose(fout);
					return(-1);
				}
				ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
				packets_in_image = 0;
			}
			
			if(verbose)
			{
				if(skipped > 0)
				{
					fprintf(stderr, "Skipped %d bytes.\n", skipped);
				}

				fprintf(stderr, "Decoded image packet. Callsign: \"%s\", Image ID: %u, Resolution: %dx%d, Packet ID: %lu (%d errors corrected)\n"
				                ">> Type: %d, Quality: %d, Huffman profile: %d, EOI: %d, MCU Mode: %d, MCU Offset: %d, MCU ID: %lu/%lu\n",
					p.callsign_s,
					p.image_id,
					p.width,
					p.height,
					(unsigned long) p.packet_id,
					errors,
					p.type,
					p.quality,
					p.huff_profile,
					p.eoi,
					p.mcu_mode,
					p.mcu_offset,
					(unsigned long) p.mcu_id,
					(unsigned long) p.mcu_count
				);
			}
			
			/* Feed it to the decoder */
			feed_result = ssdv_dec_feed(&ssdv, pkt);
			packets_in_image++;
			packets_total++;

			if(feed_result == SSDV_OK)
			{
				images_written++;
				if(!decode_output_base && images_written > 1 && !warned_stdout_multi)
				{
					fprintf(stderr, "Warning: multiple images detected while writing to stdout; JPEG data is concatenated.\n");
					warned_stdout_multi = 1;
				}

				if(write_decoded_image(&ssdv, &jpeg, &jpeg_length, decode_output_base, images_written, fout) < 0)
				{
					free(pkt);
					free(jpeg);
					if(fin != stdin) fclose(fin);
					if(fout != stdout) fclose(fout);
					return(-1);
				}

				if(ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK)
				{
					free(pkt);
					free(jpeg);
					if(fin != stdin) fclose(fin);
					if(fout != stdout) fclose(fout);
					return(-1);
				}
				ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
				packets_in_image = 0;
			}
			else if(feed_result == SSDV_ERROR)
			{
				free(pkt);
				free(jpeg);
				if(fin != stdin) fclose(fin);
				if(fout != stdout) fclose(fout);
				return(-1);
			}
			
			/* Refill buffer to start hunting for the next packet */
			for (i = 0; i < pkt_length; i++) {
				int next_byte = get_next_byte(fin, sockfd, udp_buf, &udp_buf_len, &udp_buf_pos);
				if (next_byte == EOF) { c = -1; break; }
				pkt[i] = (uint8_t)next_byte;
			}
			if (c != 0) break;
		}
end_of_stream:

		if(packets_in_image > 0)
		{
			images_written++;
			if(!decode_output_base && images_written > 1 && !warned_stdout_multi)
			{
				fprintf(stderr, "Warning: multiple images detected while writing to stdout; JPEG data is concatenated.\n");
				warned_stdout_multi = 1;
			}

			if(write_decoded_image(&ssdv, &jpeg, &jpeg_length, decode_output_base, images_written, fout) < 0)
			{
				free(pkt);
				free(jpeg);
				if(fin != stdin) fclose(fin);
				if(fout != stdout) fclose(fout);
				return(-1);
			}
		}

		free(jpeg);
		
		fprintf(stderr, "Read %i packets\n", packets_total);
		fprintf(stderr, "Decoded %i image%s\n", images_written, images_written == 1 ? "" : "s");
		
		break;
	}
	
	case 1: /* Encode */
		
		if(ssdv_enc_init(&ssdv, type, callsign, image_id, quality, pkt_length) != SSDV_OK)
		{
			free(pkt);
			return(-1);
		}
		if(ssdv_set_huffman_profile(&ssdv, huff_profile) != SSDV_OK)
		{
			fprintf(stderr, "Invalid Huffman profile (use 0 or 1)\n");
			free(pkt);
			return(-1);
		}
		
		ssdv_enc_set_buffer(&ssdv, pkt);
		
		i = 0;
		
		while(1)
		{
			while((c = ssdv_enc_get_packet(&ssdv)) == SSDV_FEED_ME)
			{
				size_t r = fread(b, 1, 128, fin);
				
				if(r <= 0)
				{
					fprintf(stderr, "Premature end of file\n");
					break;
				}
				ssdv_enc_feed(&ssdv, b, r);
			}
			
			if(c == SSDV_EOI)
			{
				fprintf(stderr, "ssdv_enc_get_packet said EOI\n");
				break;
			}
			else if(c != SSDV_OK)
			{
				fprintf(stderr, "ssdv_enc_get_packet failed: %i\n", c);
				free(pkt);
				return(-1);
			}
			
			if (udp_port > 0) {
				sendto(sockfd, (const char *)pkt, pkt_length, 0, (const struct sockaddr *)&destaddr, sizeof(destaddr));
			} else {
				fwrite(pkt, 1, pkt_length, fout);
			}
			i++;
		}
		
		fprintf(stderr, "Wrote %i packets\n", i);
		
		break;
	
	default:
		fprintf(stderr, "No mode specified.\n");
		break;
	}
	
	free(pkt);
	if(fin != stdin) fclose(fin);
	if(fout != stdout) fclose(fout);

	if(udp_port > 0) {
#ifdef _WIN32
		closesocket(sockfd);
		WSACleanup();
#else
		close(sockfd);
#endif
	}
	
	return(0);
}
