/*
 * Profiler.cpp
 *
 *  Created on: 2016.6.4
 *      Author: andyzhou
 */

#include <llvm/Support/raw_ostream.h>
#include "SABER/Profiler.h"

using namespace llvm;

#define VALUE_UNDEF -1

Profiler::Profiler(property_t property)
	: properties_to_track(property) {
	reset();
	is_snapshoted = false;
}

Profiler::~Profiler() {

}

int Profiler::pick_memory() {
	return pick_info("VmSize");
}

int Profiler::pick_peak_memory() {
	return pick_info("VmPeak");
}

int Profiler::pick_info(const char* info_name) {
	int ret = VALUE_UNDEF;

#ifdef __linux__
	char temp[2048], label[64];
	FILE *fp;

	fp = fopen("/proc/self/status", "r");
	if (fp == NULL)
		return ret;

	char* fget_res = nullptr;
	while(true) {
		fget_res = fgets(temp, 1024, fp);
		if (!fget_res) {
			ret = VALUE_UNDEF;
			break;
		}
		sscanf(temp, "%s %d", label, &ret);
		if(strncmp(label, info_name, strlen(info_name)) == 0) {
			break;
		}
	}

	fclose(fp);
#endif
	return ret;
}


void Profiler::reset_memory() {

#ifndef __linux__
	// currently we only support memory measurement on linux
	return;
#endif

	int cur_mem = pick_memory();
	last_mem = cur_mem;
}

void Profiler::snapshot_memory() {

#ifndef __linux__
	// currently we only support memory measurement on linux
	return;
#endif

	int cur_mem = pick_memory();
	if (cur_mem == VALUE_UNDEF || last_mem == VALUE_UNDEF) {
		snapshoted_memory = VALUE_UNDEF;
	} else {
		snapshoted_memory = cur_mem - last_mem;
	}
}

void Profiler::print_memory(const char* title, int memory_value) {

#ifndef __linux__
	// currently we only support memory measurement on linux
	return;
#endif
	
	if (memory_value == VALUE_UNDEF) {
		outs() << "Memory tracking for " << title << " error\n";
		return;
	}

	int mem_cost_kb_val = memory_value & 0x3FF;
	int mem_cost_in_mb = memory_value >> 10;
	int mem_cost_mb_val = mem_cost_in_mb & 0x3FF;
	int mem_cost_in_gb = mem_cost_in_mb >> 10;

	outs() << title << " Memory: \t";
	if (mem_cost_in_gb != 0) {
		outs() << mem_cost_in_gb << "G ";
	}

	if (mem_cost_in_mb != 0) {
		outs() << mem_cost_mb_val << "M ";
	}

	outs() << mem_cost_kb_val << "KB\n";
}

void Profiler::reset_time() {
	time(&last_time);
}

void Profiler::snapshot_time() {
	time_t curr_time;
	time(&curr_time);

	snapshoted_time = (size_t) difftime(curr_time, last_time);
}

void Profiler::print_time(const char* title) {
	assert (is_snapshoted && "printing the profiling result without a snapshot");

	size_t hour = snapshoted_time / 3600;
	size_t min = snapshoted_time / 60;
	size_t min_val = min % 60;
	size_t sec = snapshoted_time % 60;

	outs() << title << " Time: \t";

	if (hour != 0) {
		outs()<< hour << "h ";
	}

	if (min != 0) {
		outs()<< min_val << "m ";
	}

	outs() << sec << "s\n";
}

void Profiler::reset() {
	if (properties_to_track & TIME)
		reset_time();

	if (properties_to_track & MEMORY)
		reset_memory();

}

void Profiler::create_snapshot() {
	is_snapshoted = true;

	if (properties_to_track & TIME)
		snapshot_time();

	if (properties_to_track & MEMORY)
		snapshot_memory();
}

void Profiler::create_reset_snapshot() {
	create_snapshot();
	reset();
}

void Profiler::print_snapshot_result(const char* title) {
	if (!is_snapshoted) {
		create_snapshot();
	}

	if (properties_to_track & TIME)
		print_time(title);

	if (properties_to_track & MEMORY)
		print_memory(title, snapshoted_memory);
}

void Profiler::print_peak_memory() {
	int peak_memory = pick_peak_memory();
	print_memory("Peak", peak_memory);
}
