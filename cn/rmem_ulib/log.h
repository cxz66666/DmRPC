#pragma once

#include <chrono>
#include <string>

namespace rmem
{

// Log levels: higher means more verbose
#define RMEM_LOG_LEVEL_OFF 0
#define RMEM_LOG_LEVEL_ERROR 1   // Only fatal conditions
#define RMEM_LOG_LEVEL_WARN 2    // Conditions from which it's possible to recover
#define RMEM_LOG_LEVEL_INFO 3    // Reasonable to log (e.g., management packets)
#define RMEM_LOG_LEVEL_REORDER 4 // Too frequent to log (e.g., reordered pkts)
#define RMEM_LOG_LEVEL_TRACE 5   // Extremely frequent (e.g., all datapath pkts)
#define RMEM_LOG_LEVEL_CC 6      // Even congestion control decisions!

#define RMEM_LOG_DEFAULT_STREAM stdout

    // Log messages with "reorder" or higher verbosity get written to
    // rmem_trace_file_or_default_stream. This can be stdout for basic debugging, or
    // eRPC's trace file for more involved debugging.

#define rmem_trace_file_or_default_stream trace_file_
//#define rmem_trace_file_or_default_stream RMEM_LOG_DEFAULT_STREAM

// If RMEM_LOG_LEVEL is not defined, default to the highest level so that
// YouCompleteMe does not report compilation errors
#ifndef RMEM_LOG_LEVEL
#define RMEM_LOG_LEVEL RMEM_LOG_LEVEL_CC
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_ERROR
#define RMEM_ERROR(...)                                    \
    rmem::output_log_header(stderr, RMEM_LOG_LEVEL_ERROR); \
    fprintf(RMEM_LOG_DEFAULT_STREAM, __VA_ARGS__);         \
    fflush(RMEM_LOG_DEFAULT_STREAM)
#else
#define RMEM_ERROR(...) ((void)0)
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_WARN
#define RMEM_WARN(...)                                                     \
    rmem::output_log_header(RMEM_LOG_DEFAULT_STREAM, RMEM_LOG_LEVEL_WARN); \
    fprintf(RMEM_LOG_DEFAULT_STREAM, __VA_ARGS__);                         \
    fflush(RMEM_LOG_DEFAULT_STREAM)
#else
#define RMEM_WARN(...) ((void)0)
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_INFO
#define RMEM_INFO(...)                                                     \
    rmem::output_log_header(RMEM_LOG_DEFAULT_STREAM, RMEM_LOG_LEVEL_INFO); \
    fprintf(RMEM_LOG_DEFAULT_STREAM, __VA_ARGS__);                         \
    fflush(RMEM_LOG_DEFAULT_STREAM)
#else
#define RMEM_INFO(...) ((void)0)
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_REORDER
#define RMEM_REORDER(...)                                      \
    rmem::output_log_header(rmem_trace_file_or_default_stream, \
                            RMEM_LOG_LEVEL_REORDER);           \
    fprintf(rmem_trace_file_or_default_stream, __VA_ARGS__);   \
    fflush(rmem_trace_file_or_default_stream)
#else
#define RMEM_REORDER(...) ((void)0)
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_TRACE
#define RMEM_TRACE(...)                                        \
    rmem::output_log_header(rmem_trace_file_or_default_stream, \
                            RMEM_LOG_LEVEL_TRACE);             \
    fprintf(rmem_trace_file_or_default_stream, __VA_ARGS__);   \
    fflush(rmem_trace_file_or_default_stream)
#else
#define RMEM_TRACE(...) ((void)0)
#endif

#if RMEM_LOG_LEVEL >= RMEM_LOG_LEVEL_CC
#define RMEM_CC(...)                                           \
    rmem::output_log_header(rmem_trace_file_or_default_stream, \
                            RMEM_LOG_LEVEL_CC);                \
    fprintf(rmem_trace_file_or_default_stream, __VA_ARGS__);   \
    fflush(rmem_trace_file_or_default_stream)
#else
#define RMEM_CC(...) ((void)0)
#endif

    /// Return decent-precision time formatted as seconds:microseconds
    static std::string get_formatted_time()
    {
        const auto now = std::chrono::high_resolution_clock::now();

        const size_t sec = static_cast<size_t>(
            std::chrono::time_point_cast<std::chrono::seconds>(now)
                .time_since_epoch()
                .count());

        const size_t usec = static_cast<size_t>(
            std::chrono::time_point_cast<std::chrono::microseconds>(now)
                .time_since_epoch()
                .count());

        // Roll-over seconds every 100 seconds
        char buf[20];
        sprintf(buf, "%zu:%06zu", sec % 100,
                (usec - (sec * 1000000)) /* spare microseconds */);
        return std::string(buf);
    }

    // Output log message header
    static void output_log_header(FILE *stream, int level)
    {
        std::string formatted_time = get_formatted_time();

        const char *type;
        switch (level)
        {
        case RMEM_LOG_LEVEL_ERROR:
            type = "ERROR";
            break;
        case RMEM_LOG_LEVEL_WARN:
            type = "WARNG";
            break;
        case RMEM_LOG_LEVEL_INFO:
            type = "INFOR";
            break;
        case RMEM_LOG_LEVEL_REORDER:
            type = "REORD";
            break;
        case RMEM_LOG_LEVEL_TRACE:
            type = "TRACE";
            break;
        case RMEM_LOG_LEVEL_CC:
            type = "CONGC";
            break;
        default:
            type = "UNKWN";
        }

        fprintf(stream, "%s %s: ", formatted_time.c_str(), type);
    }

    /// Return true iff REORDER/TRACE/CC mode logging is disabled. These modes can
    /// print an unreasonable number of log messages.
    static bool is_log_level_reasonable()
    {
        return RMEM_LOG_LEVEL <= RMEM_LOG_LEVEL_INFO;
    }

} // namespace rmem
