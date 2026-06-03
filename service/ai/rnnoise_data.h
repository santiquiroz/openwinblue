/* rnnoise_data.h — minimal stub header for building without downloaded model.
 * Provides the RNNoise struct definition needed by rnn.h.
 * The actual model weights are in rnnoise_data.c (downloaded via download_model.sh).
 */
#ifndef RNNOISE_DATA_H
#define RNNOISE_DATA_H

#include "nnet.h"

/* These sizes come from the actual trained model. Stub uses 0 for all. */
#define CONV1_OUT_SIZE        32
#define CONV2_OUT_SIZE        32
#define GRU1_OUT_SIZE        96
#define GRU1_STATE_SIZE      96
#define GRU2_OUT_SIZE        96
#define GRU2_STATE_SIZE      96
#define GRU3_OUT_SIZE        384
#define GRU3_STATE_SIZE      384
#define DENSE_OUT_OUT_SIZE   32
#define VAD_DENSE_OUT_SIZE    1

typedef struct {
    LinearLayer conv1;
    LinearLayer conv2;
    LinearLayer gru1_input;
    LinearLayer gru1_recurrent;
    LinearLayer gru2_input;
    LinearLayer gru2_recurrent;
    LinearLayer gru3_input;
    LinearLayer gru3_recurrent;
    LinearLayer dense_out;
    LinearLayer vad_dense;
} RNNoise;

extern const WeightArray rnnoise_arrays[];

int init_rnnoise(RNNoise *model, const WeightArray *arrays);

#endif /* RNNOISE_DATA_H */
