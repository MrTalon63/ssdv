#include "webview.h"
#include "ssdv.h"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cstring>
#include "gui_html.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> udp_running{false};
std::thread udp_thread;

// Helper to escape strings for JSON responses to JS
std::string json_escape(const std::string& s) {
    std::string res = "\"";
    for (char c : s) {
        if (c == '"') res += "\\\"";
        else if (c == '\\') res += "\\\\";
        else if (c == '\n') res += "\\n";
        else if (c == '\r') res += "\\r";
        else if (c == '\t') res += "\\t";
        else if (c >= 0 && c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            res += buf;
        }
        else res += c;
    }
    res += "\"";
    return res;
}

// Helper to show native OS file dialogs
std::string open_file_dialog(bool save, bool multi) {
    char* filename = new char[32768](); // 32KB buffer for multiple files
#ifdef _WIN32
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = 32768;
    const char filter[] = "All Files\0*.*\0";
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (multi) ofn.Flags |= OFN_ALLOWMULTISELECT;
    
    if (save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn)) {
        std::string resStr;
        if (multi) {
            std::string dir = filename;
            char* p = filename + dir.length() + 1;
            if (*p == '\0') {
                resStr = dir; // Only one file was selected
            } else {
                while (*p) { // Multiple files were selected
                    if (!resStr.empty()) resStr += "|";
                    resStr += dir + "\\" + p;
                    p += strlen(p) + 1;
                }
            }
        } else {
            resStr = filename;
        }
        delete[] filename;
        return json_escape(resStr);
    }
#else
    std::string cmd = save ? "zenity --file-selection --save --confirm-overwrite" : "zenity --file-selection";
    if (multi) cmd += " --multiple --separator=\"|\"";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (pipe) {
        std::string res;
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe.get()) != nullptr) {
            res += buf;
        }
        if (!res.empty() && res.back() == '\n') res.pop_back();
        delete[] filename;
        if (!res.empty()) return json_escape(res);
    }
#endif
    delete[] filename;
    return "\"\"";
}

const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(const uint8_t* buf, unsigned int bufLen) {
    std::string ret;
    int i = 0, j = 0;
    uint8_t arr_3[3], arr_4[4];
    
    ret.reserve(((bufLen + 2) / 3) * 4);
    
    while (bufLen--) {
        arr_3[i++] = *(buf++);
        if (i == 3) {
            arr_4[0] = (arr_3[0] & 0xfc) >> 2;
            arr_4[1] = ((arr_3[0] & 0x03) << 4) + ((arr_3[1] & 0xf0) >> 4);
            arr_4[2] = ((arr_3[1] & 0x0f) << 2) + ((arr_3[2] & 0xc0) >> 6);
            arr_4[3] = arr_3[2] & 0x3f;
            for(i = 0; i < 4; i++) ret += base64_chars[arr_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) arr_3[j] = '\0';
        arr_4[0] = (arr_3[0] & 0xfc) >> 2;
        arr_4[1] = ((arr_3[0] & 0x03) << 4) + ((arr_3[1] & 0xf0) >> 4);
        arr_4[2] = ((arr_3[1] & 0x0f) << 2) + ((arr_3[2] & 0xc0) >> 6);
        arr_4[3] = arr_3[2] & 0x3f;
        for (j = 0; j < i + 1; j++) ret += base64_chars[arr_4[j]];
        while(i++ < 3) ret += '=';
    }
    return ret;
}

// Native C++ implementation of the SSDV Encode and Decode loop using the C API
std::string process_ssdv(const std::string& req) {
    std::vector<std::string> args;
    std::string current;
    bool in_string = false;
    
    // Parse Webview's JSON array of arguments coming from JavaScript
    for (size_t i = 0; i < req.size(); i++) {
        if (req[i] == '"' && (i == 0 || req[i-1] != '\\')) {
            in_string = !in_string;
            if (!in_string) { args.push_back(current); current.clear(); }
        } else if (in_string) {
            if (req[i] == '\\' && i+1 < req.size() && (req[i+1] == '"' || req[i+1] == '\\')) {
                current += req[i+1]; i++;
            } else { current += req[i]; }
        }
    }

    auto return_json = [](const std::string& msg) {
        return "{\"log\":" + json_escape(msg) + ",\"images\":[]}";
    };

    if (args.size() < 9) return return_json("Error: Invalid arguments.");

    bool encode = (args[0] == "-e");
    std::string in_path = args[1];
    std::string out_path = args[2];
    
    uint8_t type = SSDV_TYPE_NORMAL;
    if (args[3] == "-n") type = SSDV_TYPE_NOFEC;
    else if (args[3] == "-C") type = SSDV_TYPE_CCSDS;
    
    int pkt_length = (type == SSDV_TYPE_CCSDS) ? SSDV_PKT_SIZE_CCSDS : SSDV_PKT_SIZE;
    if (!args[4].empty()) try { pkt_length = std::stoi(args[4]); } catch (...) {}
    
    char callsign[7] = {0};
    strncpy(callsign, args[5].c_str(), 6);
    
    uint16_t image_id = 0;
    if (!args[6].empty()) try { image_id = std::stoi(args[6]); } catch (...) {}
    
    int8_t quality = 4;
    if (!args[7].empty()) try { quality = std::stoi(args[7]); } catch (...) {}
    
    uint8_t huff_profile = 1;
    if (!args[8].empty()) try { huff_profile = std::stoi(args[8]); } catch (...) {}

    std::string log_output;
    auto log = [&](const std::string& msg) { log_output += msg + "\n"; };
    std::vector<std::string> images_json;

    FILE *fin = fopen(in_path.c_str(), "rb");
    if (!fin) return return_json("Error opening input file: " + in_path);
    
    FILE *fout = nullptr;
    if (encode) {
        fout = fopen(out_path.c_str(), "wb");
        if (!fout) { fclose(fin); return return_json("Error opening output file: " + out_path); }
    }

    uint8_t* pkt = (uint8_t*)malloc(pkt_length);
    if (!pkt) { fclose(fin); if (fout) fclose(fout); return return_json("Error: Out of memory."); }

    ssdv_t ssdv;
    if (encode) {
        if (ssdv_enc_init(&ssdv, type, callsign, image_id, quality, pkt_length) != SSDV_OK) {
            free(pkt); fclose(fin); fclose(fout);
            return return_json("Error: Failed to init encoder (Check packet constraints).");
        }
        ssdv_set_huffman_profile(&ssdv, huff_profile);
        ssdv_enc_set_buffer(&ssdv, pkt);

        int packets_written = 0, c;
        uint8_t b[128];
        while(1) {
            while((c = ssdv_enc_get_packet(&ssdv)) == SSDV_FEED_ME) {
                size_t r = fread(b, 1, 128, fin);
                if (r <= 0) break;
                ssdv_enc_feed(&ssdv, b, r);
            }
            if (c == SSDV_EOI) break;
            if (c != SSDV_OK) { log("Encoder error: " + std::to_string(c)); break; }
            fwrite(pkt, 1, pkt_length, fout);
            packets_written++;
        }
        log("Successfully wrote " + std::to_string(packets_written) + " packets to " + out_path);
    } else {
        if (ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK) {
            free(pkt); fclose(fin);
            return return_json("Error: Failed to initialize decoder.");
        }
        
        size_t jpeg_length = 4 * 1024 * 1024; // 4MB buffer
        uint8_t* jpeg = (uint8_t*)malloc(jpeg_length);
        if (!jpeg) { free(pkt); fclose(fin); return return_json("Error: Out of memory."); }
        
        ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
        int packets_total = 0, images_written = 0, packets_in_image = 0;

        ssdv_packet_info_t last_info;
        memset(&last_info, 0, sizeof(last_info));

        auto write_image = [&]() {
            uint8_t* out_jpeg; size_t out_len;
            ssdv_dec_get_jpeg(&ssdv, &out_jpeg, &out_len);
            if (out_len > 0) {
                std::string img_path = out_path;
                if (images_written > 0) {
                    size_t dot = img_path.find_last_of('.');
                    if (dot == std::string::npos) img_path += "_" + std::to_string(images_written + 1);
                    else img_path = img_path.substr(0, dot) + "_" + std::to_string(images_written + 1) + img_path.substr(dot);
                }
                FILE* f = fopen(img_path.c_str(), "wb");
                if (f) {
                    fwrite(out_jpeg, 1, out_len, f); fclose(f); images_written++;
                    log("Decoded image to " + img_path + " (" + std::to_string(out_len) + " bytes)");

                    std::string b64 = base64_encode(out_jpeg, out_len);
                    std::string img_json = "{";
                    img_json += "\"path\":" + json_escape(img_path) + ",";
                    img_json += "\"callsign\":" + json_escape(last_info.callsign_s) + ",";
                    img_json += "\"imageId\":" + std::to_string(last_info.image_id) + ",";
                    img_json += "\"width\":" + std::to_string(last_info.width) + ",";
                    img_json += "\"height\":" + std::to_string(last_info.height) + ",";
                    img_json += "\"packets\":" + std::to_string(packets_in_image) + ",";
                    img_json += "\"data\":\"" + b64 + "\"";
                    img_json += "}";
                    images_json.push_back(img_json);
                }
            }
        };

        while(fread(pkt, pkt_length, 1, fin) > 0) {
            int errors, c;
            while(1) {
                if (pkt_length == SSDV_PKT_SIZE_CCSDS || pkt[0] == SSDV_PKT_SYNC) {
                    if ((c = ssdv_dec_is_packet(pkt, pkt_length, &errors)) == 0) break;
                }
                memmove(&pkt[0], &pkt[1], pkt_length - 1);
                int next_byte = fgetc(fin);
                if (next_byte == EOF) { c = -1; break; }
                pkt[pkt_length - 1] = (uint8_t)next_byte;
            }
            if (c != 0) break;

            ssdv_packet_info_t p;
            memset(&p, 0, sizeof(p));
            ssdv_dec_header(&p, pkt, pkt_length);
            last_info = p;

            if (p.packet_id == 0 && packets_in_image > 0) {
                write_image();
                ssdv_dec_init(&ssdv, pkt_length);
                ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
                packets_in_image = 0;
            }

            int feed_result = ssdv_dec_feed(&ssdv, pkt);
            packets_in_image++; packets_total++;

            if (feed_result == SSDV_OK) {
                write_image();
                ssdv_dec_init(&ssdv, pkt_length);
                ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
                packets_in_image = 0;
            } else if (feed_result == SSDV_ERROR) {
                log("Decoder error encountered."); break;
            }
        }
        if (packets_in_image > 0) write_image();
        log("Read " + std::to_string(packets_total) + " valid packets total.");
        free(jpeg);
    }

    free(pkt); fclose(fin); if (fout) fclose(fout);
    
    std::string res = "{\"log\":" + json_escape(log_output.empty() ? "Success" : log_output) + ",\"images\":[";
    for (size_t i = 0; i < images_json.size(); i++) {
        res += images_json[i];
        if (i < images_json.size() - 1) res += ",";
    }
    res += "]}";
    
    return res;
}

int main() {
    webview::webview w(true, nullptr);
    w.set_title(std::string("SSDV-NG ") + SSDV_VERSION);
    w.set_size(1100, 750, WEBVIEW_HINT_NONE);
    
    // Bind the C++ execution function to JavaScript
    w.bind("run_ssdv", [&](std::string req) -> std::string {
        return process_ssdv(req);
    });
    
    // Bind the native file picker
    w.bind("pick_file", [&](std::string req) -> std::string {
        bool save = req.find("true") != std::string::npos;
        return open_file_dialog(save, false);
    });
    
    // Bind the native multi-file picker
    w.bind("pick_files", [&](std::string req) -> std::string {
        return open_file_dialog(false, true);
    });
    
    w.bind("start_udp_listen", [&w](std::string req) -> std::string {
        if (udp_running) {
            udp_running = false;
            if (udp_thread.joinable()) udp_thread.join();
        }

        std::vector<std::string> args;
        std::string current;
        bool in_string = false;
        for (size_t i = 0; i < req.size(); i++) {
            if (req[i] == '"' && (i == 0 || req[i-1] != '\\')) {
                in_string = !in_string;
                if (!in_string) { args.push_back(current); current.clear(); }
            } else if (in_string) {
                if (req[i] == '\\' && i+1 < req.size() && (req[i+1] == '"' || req[i+1] == '\\')) {
                    current += req[i+1]; i++;
                } else { current += req[i]; }
            }
        }
        
        if (args.size() < 4) return "\"Error: Invalid args\"";

        std::string out_path = args[0];
        std::string fec = args[1];
        std::string pktLenStr = args[2];
        std::string udpHost = args[3];

        uint8_t type = SSDV_TYPE_NORMAL;
        if (fec == "-n") type = SSDV_TYPE_NOFEC;
        else if (fec == "-C") type = SSDV_TYPE_CCSDS;

        int pkt_length = (type == SSDV_TYPE_CCSDS) ? SSDV_PKT_SIZE_CCSDS : SSDV_PKT_SIZE;
        if (!pktLenStr.empty()) try { pkt_length = std::stoi(pktLenStr); } catch (...) {}

        char host[256] = "";
        int port = 0;
        size_t colon = udpHost.find_last_of(':');
        if (colon != std::string::npos) {
            strncpy(host, udpHost.substr(0, colon).c_str(), sizeof(host)-1);
            port = std::stoi(udpHost.substr(colon + 1));
        } else {
            port = std::stoi(udpHost);
        }

        udp_running = true;
        udp_thread = std::thread([&w, out_path, type, pkt_length, port, host_str = std::string(host)]() {
            char host[256];
            strncpy(host, host_str.c_str(), sizeof(host)-1);
            host[sizeof(host)-1] = '\0';
            
#ifdef _WIN32
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
            SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#else
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
#endif
            
            if (sockfd < 0) {
                w.dispatch([&w]() { w.eval("appendLog(\"Error: Failed to create UDP socket.\")"); });
                return;
            }

            int bufsize = 4 * 1024 * 1024;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char *)&bufsize, sizeof(bufsize));

            uint32_t ip = host[0] ? ntohl(inet_addr(host)) : 0;
            bool is_multicast = (ip >= 0xE0000000) && (ip <= 0xEFFFFFFF);

            int reuse = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

            struct sockaddr_in servaddr;
            memset(&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_addr.s_addr = (host[0] && !is_multicast) ? inet_addr(host) : INADDR_ANY;
            servaddr.sin_port = htons(port);

            if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                /* Fallback to 0.0.0.0 if explicit IP fails (e.g., broadcast or remote IPs) */
                servaddr.sin_addr.s_addr = INADDR_ANY;
                if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                    w.dispatch([&w]() { w.eval("appendLog(\"Error: Failed to bind socket.\")"); });
#ifdef _WIN32
                    closesocket(sockfd); WSACleanup();
#else
                    close(sockfd);
#endif
                    return;
                }
            }

            if (is_multicast) {
                struct ip_mreq mreq;
                mreq.imr_multiaddr.s_addr = inet_addr(host);
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
                w.dispatch([&w, host]() { w.eval("appendLog(\"Joined multicast group " + std::string(host) + "\")"); });
            }
            
            ssdv_t ssdv;
            if (ssdv_dec_init(&ssdv, pkt_length) != SSDV_OK) {
                w.dispatch([&w]() { w.eval("appendLog(\"Error: Failed to initialize decoder.\")"); });
#ifdef _WIN32
                closesocket(sockfd); WSACleanup();
#else
                close(sockfd);
#endif
                return;
            }

            size_t jpeg_length = 4 * 1024 * 1024;
            uint8_t* jpeg = (uint8_t*)malloc(jpeg_length);
            ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);

            uint8_t* pkt = (uint8_t*)malloc(pkt_length);
            int images_written = 0;
            int packets_in_image = 0;
            ssdv_packet_info_t last_info;
            memset(&last_info, 0, sizeof(last_info));

            auto write_image = [&]() {
                uint8_t* out_jpeg; size_t out_len;
                ssdv_dec_get_jpeg(&ssdv, &out_jpeg, &out_len);
                if (out_len > 0) {
                    std::string img_path = out_path;
                    if (!img_path.empty()) {
                        if (images_written > 0) {
                            size_t dot = img_path.find_last_of('.');
                            if (dot == std::string::npos) img_path += "_" + std::to_string(images_written + 1);
                            else img_path = img_path.substr(0, dot) + "_" + std::to_string(images_written + 1) + img_path.substr(dot);
                        }
                        FILE* f = fopen(img_path.c_str(), "wb");
                        if (f) { fwrite(out_jpeg, 1, out_len, f); fclose(f); }
                    }
                    images_written++;

                    std::string b64 = base64_encode(out_jpeg, out_len);
                    std::string img_json = "{";
                    img_json += "\"path\":" + json_escape(img_path) + ",";
                    img_json += "\"callsign\":" + json_escape(last_info.callsign_s) + ",";
                    img_json += "\"imageId\":" + std::to_string(last_info.image_id) + ",";
                    img_json += "\"width\":" + std::to_string(last_info.width) + ",";
                    img_json += "\"height\":" + std::to_string(last_info.height) + ",";
                    img_json += "\"packets\":" + std::to_string(packets_in_image) + ",";
                    img_json += "\"data\":\"" + b64 + "\"";
                    img_json += "}";

                    w.dispatch([&w, img_json]() {
                        w.eval("addLiveImage(" + img_json + ")");
                    });
                }
            };

            auto update_preview = [&]() {
                ssdv_t ssdv_copy = ssdv; // Shallow copy to preserve the real decoder state
                uint8_t* out_jpeg; size_t out_len;
                ssdv_dec_get_jpeg(&ssdv_copy, &out_jpeg, &out_len); // Safely finalize the copy
                if (out_len > 0) {
                    std::string b64 = base64_encode(out_jpeg, out_len);
                    std::string img_json = "{";
                    img_json += "\"path\":\"\",";
                    img_json += "\"callsign\":" + json_escape(last_info.callsign_s) + ",";
                    img_json += "\"imageId\":" + std::to_string(last_info.image_id) + ",";
                    img_json += "\"width\":" + std::to_string(last_info.width) + ",";
                    img_json += "\"height\":" + std::to_string(last_info.height) + ",";
                    img_json += "\"packets\":" + std::to_string(packets_in_image) + ",";
                    img_json += "\"data\":\"" + b64 + "\"";
                    img_json += "}";

                    w.dispatch([&w, img_json]() {
                        w.eval("addLiveImage(" + img_json + ")");
                    });
                }
            };

            w.dispatch([&w]() { w.eval("appendLog(\"Listening for UDP SSDV frames...\")"); });

            auto last_update = std::chrono::steady_clock::now();

            while (udp_running) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sockfd, &fds);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000; // 100ms timeout
                
                int ret = select((int)(sockfd + 1), &fds, NULL, NULL, &tv);
                if (ret > 0) {
                    uint8_t udp_buf[65536];
                    int n = recvfrom(sockfd, (char*)udp_buf, sizeof(udp_buf), 0, NULL, NULL);
                    if (n > 0) {
                        /* Hunt for SSDV packets buried anywhere inside the large UDP datagrams */
                        for (int i = 0; i < n; i++) {
                            memmove(&pkt[0], &pkt[1], pkt_length - 1);
                            pkt[pkt_length - 1] = udp_buf[i];

                            int errors;
                            if ((pkt_length == SSDV_PKT_SIZE_CCSDS || pkt[0] == SSDV_PKT_SYNC) && 
                                ssdv_dec_is_packet(pkt, pkt_length, &errors) == 0) {
                                
                                ssdv_packet_info_t p;
                                memset(&p, 0, sizeof(p));
                                ssdv_dec_header(&p, pkt, pkt_length);
                                last_info = p;

                                if (p.packet_id == 0 && packets_in_image > 0) {
                                    write_image();
                                    ssdv_dec_init(&ssdv, pkt_length);
                                    ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
                                    packets_in_image = 0;
                                }

                                int feed_result = ssdv_dec_feed(&ssdv, pkt);
                                packets_in_image++;

                                if (feed_result == SSDV_OK || feed_result == SSDV_ERROR) {
                                    if (feed_result == SSDV_OK) write_image();
                                    ssdv_dec_init(&ssdv, pkt_length);
                                    ssdv_dec_set_buffer(&ssdv, jpeg, jpeg_length);
                                    packets_in_image = 0;
                                } else {
                                    auto now = std::chrono::steady_clock::now();
                                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() > 250) {
                                        update_preview();
                                        last_update = now;
                                    }
                                }
                                
                                /* Clear the buffer so we don't double-detect the exact same packet */
                                memset(pkt, 0, pkt_length);
                            }
                        }
                    }
                }
            }

            if (packets_in_image > 0) write_image();

            free(jpeg);
            free(pkt);
#ifdef _WIN32
            closesocket(sockfd); WSACleanup();
#else
            close(sockfd);
#endif
        });

        return "\"\"";
    });

    w.bind("stop_udp_listen", [&](std::string req) -> std::string {
        if (udp_running) {
            udp_running = false;
            if (udp_thread.joinable()) udp_thread.join();
        }
        return "\"\"";
    });
    
    w.set_html(html_content);
    w.run();

    // Ensure graceful shutdown of background thread when the window closes
    if (udp_running) {
        udp_running = false;
        if (udp_thread.joinable()) udp_thread.join();
    }

    return 0;
}

// Windows GUI Subsystem requires WinMain instead of main
#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
    return main();
}
#endif