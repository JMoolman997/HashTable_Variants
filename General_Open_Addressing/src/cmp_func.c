#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <basic_func.h>

/**
 * Comparison Functions
 * Each function compares two heap-allocated data items of a specific type.
 * Returns:
 *   0  if equal
 *  -1  if not equal
 */

// Comparison function for integers
int int_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    
    int int_a = *(const int *)a;
    int int_b = *(const int *)b;
    
    return (int_a == int_b) ? 0 : -1;
}

// Comparison function for long
int long_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    
    long long_a = *(const long *)a;
    long long_b = *(const long *)b;
    
    return (long_a == long_b) ? 0 : -1;
}

// Comparison function for floats
int float_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    
    float float_a = *(const float *)a;
    float float_b = *(const float *)b;
    
    // Use a tolerance for floating-point comparison if needed
    return (float_a == float_b) ? 0 : -1;
}

// Comparison function for doubles
int double_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    
    double double_a = *(const double *)a;
    double double_b = *(const double *)b;
    
    // Use a tolerance for floating-point comparison if needed
    return (double_a == double_b) ? 0 : -1;
}

// Comparison function for characters
int char_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        fprintf(stderr, "char_cmp: NULL pointer argument\n");
        return -1;
    }
    
    char char_a = *(const char *)a;
    char char_b = *(const char *)b;
    
    return (char_a == char_b) ? 0 : -1;
}

// Comparison function for strings
int string_cmp(const void *a, const void *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    
    // Since a and b are pointers to heap-allocated strings,
    // they are of type `char **`
    const char *str_a = *(const char **)a;
    const char *str_b = *(const char **)b;
    
    if (str_a == NULL || str_b == NULL) {
        return -1;
    }
    
    return (strcmp(str_a, str_b) == 0) ? 0 : -1;
}
