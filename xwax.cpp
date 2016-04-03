#include <vector>
#include <deque>
#include <limits.h>
#include <jack/jack.h>

//Credits to xwax / Copyright (C) 2014 Mark Hills <mark@xwax.org> /GPL2
/*
 * Return: the cubic interpolation of the sample at position 2 + mu
 */

static inline double cubic_interpolate(signed short y[4], double mu)
{
    signed long a0, a1, a2, a3;
    double mu2;

    mu2 = mu*mu;
    a0 = y[3] - y[2] - y[0] + y[1];
    a1 = y[0] - y[1] - a0;
    a2 = y[2] - y[0];
    a3 = y[1];

    return (mu * mu2 * a0) + (mu2 * a1) + (mu * a2) + a3;
}

/*
 * Return: Random dither, between -0.5 and 0.5
 */

static double dither() {
    short bit;
    static short x = 0xbabe;

    /* Use a 16-bit maximal-length LFSR as our random number.
     * This is faster than rand() */

    bit = (x ^ (x >> 2) ^ (x >> 3) ^ (x >> 5)) & 1;
    x = x >> 1 | (bit << 15);

    return (double)x / 65536 - 0.5; /* not quite whole range */
}

/*
 * Build a block of PCM audio, resampled from the track
 *
 * This is just a basic resampler which has a small amount of aliasing
 * where pitch > 1.0.
 *
 * Return: number of seconds advanced in the source audio track
 * Post: buffer at pcm is filled with the given number of samples
 */

static void build_pcm(std::deque<signed short> *dq,
                        unsigned first_sample, jack_nframes_t samples, double pitch,
                        jack_default_audio_sample_t *out1, jack_default_audio_sample_t *out2)
{

    short channels=2;


    unsigned int s;
    double sample, step;

    //xwax:
    //step = sample_dt * pitch * samplerate;
    //sampledt=1.0/samplerate

    sample=1.0*first_sample;
    step = channels*pitch;

    for (s = 0; s < samples; s++) {
        int c, sa, q;
        double f;
        signed short i[channels][4];

        /* 4-sample window for interpolation */

        sa = (int)sample;
        if (sample < 0.0)
            sa--;
        f = sample - sa;
        sa--;

        for (q = 0; q < 4; q++, sa++) {
            if (sa < 0 || sa >= dq->size()/channels) {
                for (c = 0; c < channels; c++)
                    i[c][q] = 0;
            } else {
                for (int c = 0; c < channels; c++)
                    i[c][q] = dq->at(sa+c);
            }
        }

        //Each RAWPCM Sample is stored as 16bit (short); We have a stereo rawpcm
        //so it's L1 R1 L2 R2...; Jack however requires us to use floats
        //Short is from -32768 to 32767, so we got to divide by 32768 (which is 2^15)
        float magic=32768.0;

        double v;
        //1st Channel
        v = cubic_interpolate(i[0], f) + dither();

        if (v >= SHRT_MAX) {
            *out1=1.0;
        } else if (v < SHRT_MIN) {
            *out1=-1.0;
        } else {
            *out1= ((signed short)v)*1.0/magic;
        }
        ++out1;
        //2nd Channel
        v = cubic_interpolate(i[1], f) + dither();

        if (v > SHRT_MAX) {
            *out2=1.0;
        } else if (v < SHRT_MIN) {
            *out2=-1.0;
        } else {
            *out2= ((signed short)v)*1.0/magic;
        }
        ++out2;


        sample += step;
    }
}

/*
 * Equivalent to build_pcm, but for use when the track is
 * not available
 *
 * Return: number of seconds advanced in the audio track
 * Post: buffer at pcm is filled with silence

static void build_silence(signed short *pcm, unsigned samples)
{
    memset(pcm, '\0', sizeof(*pcm) * PLAYER_CHANNELS * samples);
    return sample_dt * pitch * samples;
}
*/
