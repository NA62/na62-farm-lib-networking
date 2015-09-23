//#include <libpcap/pcap.h>
// g++ pcapFile.c -I /usr/include/ -L ../ -lpcap -lrt -o pcapFile

#include "PcapDumper.h"
#include <time.h>

namespace na62 {

    void PcapDumper::dumpPacket(char* packet, uint packet_leght) {
      pcap_pkthdr pcap_hdr;

      //pcap_hdr.caplen = sizeof(pkt1);
      //pcap_hdr.len = pcap_hdr.caplen;
      pcap_hdr.caplen = pcap_hdr.len = packet_leght;
      gettimeofday(&pcap_hdr.ts, NULL);

      pcap_dump((u_char *)dumper_, &pcap_hdr, pkt1);
    }

    PcapDumper::PcapDumper() {
    }
    PcapDumper::~PcapDumper() {
    }
}
