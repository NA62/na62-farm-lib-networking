#include "PcapDumper.h"
#include <time.h>

namespace na62 {

const int PcapDumper::n_file_;
pcap_t* PcapDumper::handle_[n_file_];
pcap_dumper_t* PcapDumper::dumper_[n_file_];

void PcapDumper::dumpPacket(int file_n, char* packet, uint packet_leght) {

	pcap_pkthdr pcap_hdr;
	pcap_hdr.caplen = pcap_hdr.len = packet_leght;
	gettimeofday(&pcap_hdr.ts, NULL);

	pcap_dump((u_char *) dumper_[file_n], &pcap_hdr, (u_char *) packet);
}

PcapDumper::PcapDumper() {
}
PcapDumper::~PcapDumper() {
}
}
