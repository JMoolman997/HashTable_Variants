#ifndef DEBUG_HASHTAB_H
#define DEBUG_HASHTAB_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define MAX_MESSAGE_LENGTH 1024

#ifdef DEBUG_HASHTAB

/* Indentation level for nested debug messages */
static int indent = 0;

static FILE *debug_log_file = NULL;

/**
 * @brief Initializes the debug logging by opening the specified log file.
 *
 * @param filename The path to the log file.
 *
 * @return 0 on success, -1 on failure.
 */
static inline int debug_init(
		const char *filename
) {
    if (debug_log_file != NULL) {
        // Already initialized
        return 0;
    }

    debug_log_file = fopen(filename, "w");
    if (debug_log_file == NULL) {
        fprintf(stderr, "Failed to open debug log file: %s\n", filename);
        return -1;
    }

    return 0;
}

/**
 * @brief Closes the debug log file if it is open.
 */
static inline void debug_close(
		void
) {
    if (debug_log_file != NULL) {
        fclose(debug_log_file);
        debug_log_file = NULL;
    }
}

/**
 * @brief Prints debug information with the current indentation.
 *
 * @param fmt The format string.
 * @param ap  The variable argument list.
 */
static inline void vdebug_info(
		const char* fmt,
		va_list ap
) {
	int i;
    if (debug_log_file == NULL) {
        /* If debug_log_file is not initialized, default to stderr */
        debug_log_file = stderr;
    }

    /* Add indentation */
    for (i = 0; i < indent; i++) {
        fputc(' ', debug_log_file);
    }

    /* Format the message */
    vfprintf(debug_log_file, fmt, ap);
    fputc('\n', debug_log_file);

}

/**
 * @brief Prints a debug information message.
 *
 * @param fmt The format string.
 * @param ... Variable arguments.
 */
static inline void debug_info(
		const char* fmt,
		...
) {
    va_list ap;
    va_start(ap, fmt);
    vdebug_info(fmt, ap);
    va_end(ap);
}

/**
 * @brief Marks the start of a debug block and increases indentation.
 *
 * @param fmt The format string.
 * @param ... Variable arguments.
 */
static inline void debug_start(
		const char* fmt,
		...
) {
	va_list ap;

	va_start(ap, fmt);
	vdebug_info(fmt, ap);
	va_end(ap);
	indent += 2;
}

/**
 * @brief Marks the end of a debug block and decreases indentation.
 *
 * @param fmt The format string.
 * @param ... Variable arguments.
 */
static inline void debug_end(
		const char* fmt,
		...
) {
	va_list ap;
	indent -= 2;
	if (indent < 0) {
		indent = 0;
	}

	va_start(ap, fmt);
	vdebug_info(fmt, ap);
	va_end(ap);
}

#define DBG_start(...) debug_start(__VA_ARGS__)
#define DBG_end(...)   debug_end(__VA_ARGS__)
#define DBG_info(...)  debug_info(__VA_ARGS__)

/* Ensure that the debug log file is closed when the program exits */
static inline void debug_cleanup(
		void
) {
    debug_close();
}

/* Use GCC's constructor and destructor attributes to initialize and clean up */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void debug_auto_init() {
    // Initialize with a default log file; can be overridden manually
    debug_init("debug_hashtab.log");
}

__attribute__((destructor)) static void debug_auto_cleanup() {
    debug_cleanup();
}
#endif

#else

#define DBG_start(...)
#define DBG_end(...)
#define DBG_info(...)

#endif /* DEBUG_HASHTAB */

#endif /* DEBUG_HASHTAB_H */
