/*
 * Profile.h
 *
 *  Created on: 2016.6.4
 *      Author: andyzhou
 */

#ifndef PLATFORM_OS_PROFILER_H
#define PLATFORM_OS_PROFILER_H

#include <stdbool.h>
#include <time.h>
#include <string>


// This class the profiling methods
// Note: This class is NOT thread safe,
//       please use different instances of profilers for different threads
//
// Example of usage:
//
// 		reset();
// 		first_func(...);
// 		create_snapshot_n_reset();
// 		print_snapshot_result("First Function") // print the profiling result for first_func()
// 		second_func(...);
//		create_snapshot();
// 		print_snapshot_result("First Function") // print the profiling result for second_func()
class Profiler {
public:
	typedef uint64_t property_t;

private:
	int last_mem;
	int snapshoted_memory;

	time_t last_time;
	size_t snapshoted_time;

	bool is_snapshoted;

	property_t properties_to_track;

private:
	// reset the corresponding property to the current value
	void reset_memory();
	void reset_time();

	// snapshot the corresponding property value;
	void snapshot_time();
	void snapshot_memory();

	// print the LATEST corresponding snapshoted property value.
	// the "title" text is used to distinguish the printed values
	// Will create a snapshot if no snapshot is created
	void print_time(const char* title);
	void print_memory(const char* title, int memory_value);

private:
	// Get the current memory usage
	int pick_memory();

	// Get the peak memory usage
	int pick_peak_memory();

	// Get the raw profiler info with name info_name
	int pick_info(const char* info_name);

public:
	// We use a 64 bit bit-vector to denote the properties to track in a profiler
	// bit 0 : time profiling
	// bit 1 : memory profiling
	static const property_t TIME = 0x1;
	static const property_t MEMORY = 0x2;

	Profiler(property_t property = TIME | MEMORY);
	~Profiler();

	// reset the profiled properties to the current values
	void reset();

	// create snapshot for all the profiled properties
	void create_snapshot();

	// create snapshot and reset;
	void create_reset_snapshot();

	// print the snapshot value on the screen with the "title" text to distinguish the data
	void print_snapshot_result(const char* title);

	// print the peak memory during the program execution
	void print_peak_memory();
};

#endif /* PLATFORM_OS_PROFILER_H */
