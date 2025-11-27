#ifndef NSET_ENTROPY_H
#define NSET_ENTROPY_H

#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>

// A lightweight Bigram Model (64KB memory)
// It tracks: "How often does character B follow character A?"
typedef struct {
    uint32_t counts[256][256];
    uint32_t totals[256];
} EntropyModel;

// 1. TEACH the model (Online Learning)
static inline void model_train_sequence(EntropyModel *m, const char *text, int len) {
    if (len < 2) return;
    for (int i = 0; i < len - 1; i++) {
        uint8_t cur = (uint8_t)text[i];
        uint8_t next = (uint8_t)text[i+1];
        m->counts[cur][next]++;
        m->totals[cur]++;
    }
}

// 2. QUERY the model (Rényi Entropy)
// Returns a "Surprise Score" (0.0 to ~10.0)
static inline float calculate_surprise(EntropyModel *m, uint8_t cur, uint8_t next) {
    // If we haven't seen 'cur' enough times, we can't judge. Default to "No Surprise".
    if (m->totals[cur] < 5) return 0.0f; 

    float count = (float)m->counts[cur][next];
    float total = (float)m->totals[cur];
    
    // Probability P(next | cur)
    // We add mild smoothing (+0.1) to avoid division by zero errors
    float p = (count + 0.1f) / (total + 1.0f);

    // Rényi Entropy (alpha=2) is -log(p^2)
    // We simplify to -log(p) * 2 for speed, which acts as a "Surprise" metric.
    return -log2f(p);
}

#endif
