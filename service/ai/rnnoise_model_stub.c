/* rnnoise_model_stub.c
 * Provides minimal symbol stubs so rnnoise compiles without the downloaded
 * model data (rnnoise_data.c). When these stubs are active:
 *   - rnnoise_create() returns NULL (init fails gracefully)
 *   - NoiseReducer::process() passes audio through unchanged
 *
 * To enable real noise reduction: run third-party/rnnoise/download_model.sh
 */
#include "nnet.h"

/* Empty sentinel-terminated weight array — no model layers */
const WeightArray rnnoise_arrays[] = {
    { NULL, 0, 0, NULL }  /* sentinel */
};

/* Returns -1 so rnnoise_create() → rnnoise_init() fails and returns NULL.
 * NoiseReducer ctor stores NULL state_ and process() safely passes through. */
int init_rnnoise(void *model, const WeightArray *arrays) {
    (void)model;
    (void)arrays;
    return -1;
}

/* parse_weights is #define'd to rnn_parse_weights in nnet.h */
int rnn_parse_weights(WeightArray **list, const void *data, int len) {
    (void)list; (void)data; (void)len;
    return -1;
}
